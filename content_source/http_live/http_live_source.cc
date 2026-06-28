/*
 * http_live_source.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "content_source/http_live/http_live_source.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "base/data_source/data_source.h"
#include "base/logging.h"
#include "base/time_utils.h"
#include "base/units/time_delta.h"
#include "base/units/timestamp.h"
#include "media/foundation/media_mimes.h"
#include "media/modules/mpeg2ts/packet_source.h"
#include "media/modules/mpeg2ts/ts_parser.h"

namespace ave {
namespace player {

namespace {

struct TsPacketLayout {
  size_t sync_offset = 0;
  size_t packet_stride = 188;
};

constexpr size_t kTsPacketSize = 188;
constexpr size_t kM2tsPacketSize = 192;
constexpr size_t kTsSniffPacketCount = 5;
constexpr size_t kMaxTsSyncOffset = kM2tsPacketSize - 1;
constexpr size_t kMaxTsPacketSpacing = 204;
constexpr size_t kResyncProbeSize = kTsPacketSize + 512;
constexpr size_t kResyncAdvance = 256;

class MemoryDataSource : public DataSource {
 public:
  MemoryDataSource(std::string uri, std::vector<uint8_t> data)
      : uri_(std::move(uri)), data_(std::move(data)) {}

  status_t InitCheck() const override { return OK; }

  ssize_t Read(void* data, size_t size) override {
    const ssize_t bytes_read = ReadAt(position_, data, size);
    if (bytes_read > 0) {
      position_ += bytes_read;
    }
    return bytes_read;
  }

  ssize_t ReadAt(off64_t offset, void* data, size_t size) override {
    if (offset < 0 || static_cast<size_t>(offset) >= data_.size()) {
      return 0;
    }
    const size_t available = data_.size() - static_cast<size_t>(offset);
    const size_t to_copy = std::min(size, available);
    if (to_copy > 0) {
      std::memcpy(data, data_.data() + offset, to_copy);
    }
    return static_cast<ssize_t>(to_copy);
  }

  ssize_t Seek(off64_t position, int whence) override {
    off64_t target = position_;
    switch (whence) {
      case SEEK_SET:
        target = position;
        break;
      case SEEK_CUR:
        target = position_ + position;
        break;
      case SEEK_END:
        target = static_cast<off64_t>(data_.size()) + position;
        break;
      default:
        return BAD_VALUE;
    }

    if (target < 0) {
      return BAD_VALUE;
    }

    position_ = std::min<off64_t>(target, static_cast<off64_t>(data_.size()));
    return position_;
  }

  status_t GetPosition(off64_t* position) override {
    if (!position) {
      return BAD_VALUE;
    }
    *position = position_;
    return OK;
  }

  status_t GetSize(off64_t* size) override {
    if (!size) {
      return BAD_VALUE;
    }
    *size = static_cast<off64_t>(data_.size());
    return OK;
  }

  std::string GetUri() override { return uri_; }

  int32_t Flags() override { return kSeekable; }

 private:
  std::string uri_;
  std::vector<uint8_t> data_;
  off64_t position_ = 0;
};

std::shared_ptr<media::MediaMeta> CloneMeta(
    const std::shared_ptr<media::MediaMeta>& meta) {
  return meta ? std::make_shared<media::MediaMeta>(*meta) : nullptr;
}

bool HasSyncPattern(const uint8_t* data,
                    size_t size,
                    size_t sync_offset,
                    size_t packet_size) {
  const size_t required =
      sync_offset + (kTsSniffPacketCount - 1) * packet_size + 1;
  if (size < required) {
    return false;
  }

  for (size_t i = 0; i < kTsSniffPacketCount; ++i) {
    if (data[sync_offset + i * packet_size] != 0x47) {
      return false;
    }
  }
  return true;
}

bool SniffTsSegment(const std::shared_ptr<DataSource>& data_source,
                    TsPacketLayout* layout) {
  if (!data_source || !layout) {
    return false;
  }

  std::array<uint8_t, kMaxTsSyncOffset + kTsSniffPacketCount * kM2tsPacketSize>
      probe = {};
  const ssize_t bytes_read = data_source->ReadAt(0, probe.data(), probe.size());
  if (std::cmp_less(bytes_read, kTsPacketSize * kTsSniffPacketCount)) {
    return false;
  }

  for (size_t sync_offset = 0; sync_offset <= kMaxTsSyncOffset; ++sync_offset) {
    if (HasSyncPattern(probe.data(), bytes_read, sync_offset, kTsPacketSize)) {
      layout->sync_offset = sync_offset;
      layout->packet_stride = kTsPacketSize;
      return true;
    }

    if (HasSyncPattern(probe.data(), bytes_read, sync_offset,
                       kM2tsPacketSize)) {
      layout->sync_offset = sync_offset;
      layout->packet_stride = kM2tsPacketSize;
      return true;
    }
  }

  return false;
}

bool UsesFragmentedMp4(const http_live::Playlist& playlist) {
  return playlist.has_init_segment;
}

bool LooksLikePacketStart(const uint8_t* data, size_t size, size_t candidate) {
  if (candidate >= size || data[candidate] != 0x47) {
    return false;
  }

  if (candidate + kTsPacketSize >= size) {
    return true;
  }

  for (size_t delta = kTsPacketSize; delta <= kMaxTsPacketSpacing; ++delta) {
    const size_t next = candidate + delta;
    if (next < size && data[next] == 0x47) {
      return true;
    }
  }

  return false;
}

bool IsTsTrackFormatReady(const std::shared_ptr<media::MediaMeta>& format) {
  if (!format || format->mime().empty()) {
    return false;
  }

  if (format->mime() == media::MEDIA_MIMETYPE_VIDEO_AVC) {
    return format->private_data() && format->width() > 0 &&
           format->height() > 0;
  }

  if (format->mime() == media::MEDIA_MIMETYPE_AUDIO_AAC) {
    return format->private_data() && format->sample_rate() > 0;
  }

  return true;
}

class TsSegmentExtractor {
 public:
  explicit TsSegmentExtractor(std::shared_ptr<DataSource> data_source)
      : data_source_(std::move(data_source)) {}

  status_t Parse() {
    if (!SniffTsSegment(data_source_, &layout_)) {
      return media::ERROR_UNSUPPORTED;
    }

    parser_ = std::make_unique<media::mpeg2ts::TSParser>();
    offset_ = static_cast<off64_t>(layout_.sync_offset);

    for (;;) {
      const status_t err = FeedMore();
      MaybeAddTrack(media::mpeg2ts::TSParser::VIDEO);
      MaybeAddTrack(media::mpeg2ts::TSParser::AUDIO);

      if (err == media::ERROR_END_OF_STREAM) {
        break;
      }
      if (err != OK) {
        return err;
      }
    }

    MaybeAddTrack(media::mpeg2ts::TSParser::VIDEO);
    MaybeAddTrack(media::mpeg2ts::TSParser::AUDIO);

    return audio_source_ || video_source_
               ? static_cast<status_t>(OK)
               : static_cast<status_t>(media::ERROR_UNSUPPORTED);
  }

  std::shared_ptr<media::mpeg2ts::PacketSource> GetSource(
      MediaType media_type) const {
    switch (media_type) {
      case MediaType::AUDIO:
        return audio_source_;
      case MediaType::VIDEO:
        return video_source_;
      default:
        return nullptr;
    }
  }

 private:
  void SignalParserEOS(status_t result) {
    if (!eos_signaled_ && parser_) {
      parser_->SignalEOS(result);
      eos_signaled_ = true;
    }
  }

  void MaybeAddTrack(media::mpeg2ts::TSParser::SourceType type) {
    auto& track =
        type == media::mpeg2ts::TSParser::AUDIO ? audio_source_ : video_source_;
    if (track || !parser_) {
      return;
    }

    auto source = parser_->GetSource(type);
    if (!source || !IsTsTrackFormatReady(source->GetFormat())) {
      return;
    }

    track = std::move(source);
  }

  status_t FeedMore() {
    if (eos_signaled_) {
      return media::ERROR_END_OF_STREAM;
    }

    std::array<uint8_t, kResyncProbeSize> probe{};
    std::array<uint8_t, kTsPacketSize> packet{};

    for (;;) {
      const ssize_t probe_read =
          data_source_->ReadAt(offset_, probe.data(), probe.size());
      if (std::cmp_less(probe_read, kTsPacketSize)) {
        const status_t final_result =
            probe_read < 0 ? static_cast<status_t>(probe_read)
                           : static_cast<status_t>(media::ERROR_END_OF_STREAM);
        SignalParserEOS(final_result);
        return final_result;
      }

      bool found_candidate = false;
      const size_t limit = static_cast<size_t>(probe_read) - kTsPacketSize + 1;
      for (size_t candidate = 0; candidate < limit; ++candidate) {
        if (!LooksLikePacketStart(probe.data(), static_cast<size_t>(probe_read),
                                  candidate)) {
          continue;
        }

        const off64_t packet_offset = offset_ + static_cast<off64_t>(candidate);
        const ssize_t bytes_read =
            data_source_->ReadAt(packet_offset, packet.data(), packet.size());
        if (std::cmp_less(bytes_read, packet.size())) {
          const status_t final_result =
              bytes_read < 0
                  ? static_cast<status_t>(bytes_read)
                  : static_cast<status_t>(media::ERROR_END_OF_STREAM);
          SignalParserEOS(final_result);
          return final_result;
        }

        media::mpeg2ts::TSParser::SyncEvent event(packet_offset);
        const status_t err =
            parser_->FeedTSPacket(packet.data(), packet.size(), &event);
        if (err == media::ERROR_MALFORMED) {
          offset_ = packet_offset + 1;
          found_candidate = true;
          break;
        }

        offset_ = packet_offset + static_cast<off64_t>(layout_.packet_stride);
        if (err != OK) {
          SignalParserEOS(err);
        }
        return err;
      }

      if (found_candidate) {
        continue;
      }

      if (std::cmp_less(probe_read, sizeof(probe))) {
        SignalParserEOS(media::ERROR_END_OF_STREAM);
        return media::ERROR_END_OF_STREAM;
      }

      offset_ += kResyncAdvance;
    }
  }

  std::shared_ptr<DataSource> data_source_;
  std::unique_ptr<media::mpeg2ts::TSParser> parser_;
  std::shared_ptr<media::mpeg2ts::PacketSource> audio_source_;
  std::shared_ptr<media::mpeg2ts::PacketSource> video_source_;
  TsPacketLayout layout_;
  off64_t offset_ = 0;
  bool eos_signaled_ = false;
};

}  // namespace

HttpLiveSource::HttpLiveSource(std::shared_ptr<DemuxerFactory> demuxer_factory,
                               std::shared_ptr<HTTPProvider> http_provider)
    : demuxer_factory_(std::move(demuxer_factory)),
      http_provider_(std::move(http_provider)) {}

HttpLiveSource::~HttpLiveSource() = default;

status_t HttpLiveSource::SetDataSource(
    const char* url,
    const std::unordered_map<std::string, std::string>& headers) {
  if (!url) {
    return BAD_VALUE;
  }
  std::scoped_lock lock(lock_);
  ResetLocked();
  master_url_ = url;
  headers_ = headers;
  return OK;
}

void HttpLiveSource::SetNotify(Notify* notify) {
  std::scoped_lock lock(lock_);
  notify_ = notify;
}

void HttpLiveSource::Prepare() {
  status_t err = OK;
  Notify* notify = nullptr;
  std::shared_ptr<media::MediaMeta> video_format;
  int32_t flags = FLAG_CAN_PAUSE;

  {
    std::scoped_lock lock(lock_);
    err = PrepareLocked();
    notify = notify_;
    if (err == OK) {
      if (playlist_.is_live) {
        flags |= FLAG_DYNAMIC_DURATION;
      } else {
        flags |= FLAG_CAN_SEEK | FLAG_CAN_SEEK_BACKWARD | FLAG_CAN_SEEK_FORWARD;
      }
      const TrackState* video = FindTrackLocked(MediaType::VIDEO);
      if (video) {
        video_format = video->format;
      }
    }
  }

  if (!notify) {
    return;
  }
  if (err == OK) {
    notify->OnFlagsChanged(flags);
    if (video_format) {
      notify->OnVideoSizeChanged(video_format);
    }
  }
  notify->OnPrepared(err);
}

void HttpLiveSource::Start() {
  std::scoped_lock lock(lock_);
  started_ = true;
}

void HttpLiveSource::Stop() {
  std::scoped_lock lock(lock_);
  started_ = false;
}

void HttpLiveSource::Pause() {
  std::scoped_lock lock(lock_);
  started_ = false;
}

void HttpLiveSource::Resume() {
  std::scoped_lock lock(lock_);
  started_ = true;
}

status_t HttpLiveSource::DequeueAccessUnit(
    MediaType track_type,
    std::shared_ptr<media::MediaFrame>& access_unit) {
  std::scoped_lock lock(lock_);
  access_unit.reset();

  if (!prepared_ || !started_) {
    return WOULD_BLOCK;
  }

  TrackState* track = FindTrackLocked(track_type);
  if (!track || !track->packet_source) {
    return WOULD_BLOCK;
  }

  status_t result = OK;
  if (!track->packet_source->HasBufferAvailable(&result)) {
    return end_of_stream_ ? static_cast<status_t>(media::ERROR_END_OF_STREAM)
                          : static_cast<status_t>(WOULD_BLOCK);
  }

  return track->packet_source->DequeueAccessUnit(access_unit);
}

std::shared_ptr<media::MediaMeta> HttpLiveSource::GetFormat() {
  std::scoped_lock lock(lock_);
  return source_format_;
}

status_t HttpLiveSource::GetDuration(int64_t* duration_us) {
  if (!duration_us) {
    return BAD_VALUE;
  }
  std::scoped_lock lock(lock_);
  *duration_us = duration_us_;
  return duration_us_ >= 0 ? OK : INVALID_OPERATION;
}

size_t HttpLiveSource::GetTrackCount() const {
  std::scoped_lock lock(lock_);
  return tracks_.size();
}

std::shared_ptr<media::MediaMeta> HttpLiveSource::GetTrackInfo(
    size_t track_index) const {
  std::scoped_lock lock(lock_);
  if (track_index >= tracks_.size()) {
    return nullptr;
  }
  return tracks_[track_index].format;
}

std::shared_ptr<media::MediaMeta> HttpLiveSource::GetTrackInfo(
    MediaType track_type) const {
  std::scoped_lock lock(lock_);
  const TrackState* track = FindTrackLocked(track_type);
  return track ? track->format : nullptr;
}

status_t HttpLiveSource::SelectTrack(size_t track_index, bool select) {
  std::scoped_lock lock(lock_);
  if (track_index >= tracks_.size()) {
    return INVALID_OPERATION;
  }
  if (!select) {
    return INVALID_OPERATION;
  }
  return OK;
}

status_t HttpLiveSource::SeekTo(int64_t seek_time_us, SeekMode /* mode */) {
  {
    std::scoped_lock lock(lock_);
    if (playlist_.is_live || playlist_.segments.empty()) {
      return INVALID_OPERATION;
    }

    for (auto& track : tracks_) {
      if (track.packet_source) {
        track.packet_source->Clear();
      }
    }

    end_of_stream_ = false;
    bool found_segment = false;
    for (StreamState* stream : {&primary_stream_, &audio_stream_}) {
      if (!stream->active()) {
        continue;
      }

      stream->next_segment_sequence = stream->playlist.media_sequence;
      stream->next_segment_start_time_us = 0;

      int64_t accumulated_us = 0;
      bool stream_found_segment = false;
      for (const auto& segment : stream->playlist.segments) {
        if (accumulated_us + segment.duration_us > seek_time_us) {
          stream->next_segment_sequence = segment.sequence;
          stream->next_segment_start_time_us = accumulated_us;
          stream_found_segment = true;
          found_segment = true;
          break;
        }
        accumulated_us += segment.duration_us;
      }

      if (!stream_found_segment) {
        stream->next_segment_sequence =
            stream->playlist.media_sequence +
            static_cast<int32_t>(stream->playlist.segments.size());
        stream->next_segment_start_time_us = accumulated_us;
      }
    }

    if (!found_segment) {
      end_of_stream_ = true;
      return media::ERROR_END_OF_STREAM;
    }
  }

  return FeedMoreESData();
}

bool HttpLiveSource::IsStreaming() const {
  std::scoped_lock lock(lock_);
  return primary_stream_.is_live() || audio_stream_.is_live();
}

status_t HttpLiveSource::FeedMoreESData() {
  std::scoped_lock lock(lock_);
  if (!prepared_) {
    return INVALID_OPERATION;
  }
  if (end_of_stream_) {
    return media::ERROR_END_OF_STREAM;
  }

  bool any_active = false;
  bool all_eos = true;
  for (StreamState* stream : {&primary_stream_, &audio_stream_}) {
    if (!stream->active()) {
      continue;
    }
    any_active = true;
    status_t err = LoadNextSegmentLocked(*stream);
    if (err == OK || err == WOULD_BLOCK) {
      all_eos = false;
      continue;
    }
    if (err != media::ERROR_END_OF_STREAM) {
      return err;
    }
  }

  if (!any_active || all_eos) {
    end_of_stream_ = true;
    return media::ERROR_END_OF_STREAM;
  }
  return OK;
}

status_t HttpLiveSource::PrepareLocked() {
  if (!http_provider_) {
    return NO_INIT;
  }
  if (master_url_.empty()) {
    return BAD_VALUE;
  }

  status_t err = RefreshPlaylistLocked(true);
  if (err != OK) {
    return err;
  }

  for (StreamState* stream : {&primary_stream_, &audio_stream_}) {
    if (!stream->active()) {
      continue;
    }
    err = LoadNextSegmentLocked(*stream);
    if (err != OK && err != media::ERROR_END_OF_STREAM) {
      return err;
    }
  }

  return FinishPrepareLocked();
}

status_t HttpLiveSource::RefreshPlaylistLocked(bool initial) {
  if (!initial) {
    return RefreshStreamPlaylistLocked(primary_stream_, false);
  }

  primary_stream_ = StreamState();
  audio_stream_ = StreamState();

  std::string text;
  status_t err = FetchTextLocked(master_url_, text);
  if (err != OK) {
    return err;
  }

  http_live::Playlist parsed;
  err = http_live::ParsePlaylist(master_url_, text, parsed);
  if (err != OK) {
    return err;
  }
  if (parsed.is_encrypted) {
    return media::ERROR_UNSUPPORTED;
  }

  if (parsed.is_master) {
    const auto* variant = http_live::SelectPrimaryVariant(parsed);
    if (!variant) {
      return BAD_VALUE;
    }

    AVE_LOG(LS_INFO) << "HttpLiveSource selected variant: uri=" << variant->uri
                     << " bandwidth=" << variant->bandwidth_bps
                     << " codecs=" << variant->codecs;
    primary_stream_.playlist_url = variant->uri;
    err = RefreshStreamPlaylistLocked(primary_stream_, true);
    if (err != OK) {
      return err;
    }

    const auto* audio_rendition =
        http_live::SelectAudioRendition(parsed, variant->audio_group_id);
    if (audio_rendition) {
      audio_stream_.playlist_url = audio_rendition->uri;
      AVE_LOG(LS_INFO) << "HttpLiveSource selected audio rendition: uri="
                       << audio_rendition->uri
                       << " group_id=" << audio_rendition->group_id;
      err = RefreshStreamPlaylistLocked(audio_stream_, true);
      if (err != OK) {
        return err;
      }
    }
  } else {
    primary_stream_.playlist_url = master_url_;
    err = RefreshStreamPlaylistLocked(primary_stream_, true);
    if (err != OK) {
      return err;
    }
  }

  playlist_ = primary_stream_.playlist;
  if (audio_stream_.active()) {
    duration_us_ = std::max(primary_stream_.playlist.duration_us,
                            audio_stream_.playlist.duration_us);
    playlist_.is_live = playlist_.is_live || audio_stream_.playlist.is_live;
  } else {
    duration_us_ = playlist_.duration_us;
  }

  if (!source_format_) {
    source_format_ = media::MediaMeta::CreatePtr(
        MediaType::UNKNOWN, media::MediaMeta::FormatType::kTrack);
    source_format_->SetMime("application/x-mpegURL");
    source_format_->SetDuration(base::TimeDelta::Micros(duration_us_));
  } else {
    source_format_->SetDuration(base::TimeDelta::Micros(duration_us_));
  }

  return OK;
}

status_t HttpLiveSource::RefreshStreamPlaylistLocked(StreamState& stream,
                                                     bool initial) {
  if (stream.playlist_url.empty()) {
    return BAD_VALUE;
  }

  std::string text;
  status_t err = FetchTextLocked(stream.playlist_url, text);
  if (err != OK) {
    return err;
  }

  http_live::Playlist parsed;
  err = http_live::ParsePlaylist(stream.playlist_url, text, parsed);
  if (err != OK) {
    return err;
  }
  if (parsed.is_master || parsed.is_encrypted) {
    return media::ERROR_UNSUPPORTED;
  }
  if (UsesFragmentedMp4(parsed) && !demuxer_factory_) {
    return media::ERROR_UNSUPPORTED;
  }

  if (initial) {
    stream.next_segment_sequence = parsed.media_sequence;
    stream.next_segment_start_time_us = 0;
  } else if (stream.next_segment_sequence < parsed.media_sequence) {
    stream.next_segment_sequence = parsed.media_sequence;
  }

  if (stream.init_segment_cache_key != parsed.init_segment.uri) {
    stream.init_segment_bytes.clear();
  }
  stream.init_segment_cache_key = parsed.init_segment.uri;
  stream.playlist = std::move(parsed);

  if (!stream.playlist.is_live || stream.playlist.target_duration_us <= 0) {
    stream.next_playlist_refresh_time_us = 0;
  } else {
    stream.next_playlist_refresh_time_us =
        base::TimeMicros() +
        std::max<int64_t>(500000, stream.playlist.target_duration_us / 2);
  }
  return OK;
}

status_t HttpLiveSource::LoadNextSegmentLocked() {
  return LoadNextSegmentLocked(primary_stream_);
}

status_t HttpLiveSource::LoadNextSegmentLocked(StreamState& stream) {
  const int32_t first_sequence = stream.playlist.media_sequence;
  const int32_t last_sequence =
      first_sequence + static_cast<int32_t>(stream.playlist.segments.size());

  if (stream.next_segment_sequence < first_sequence) {
    stream.next_segment_sequence = first_sequence;
  }

  if (stream.next_segment_sequence >= last_sequence) {
    if (!stream.playlist.is_live) {
      return media::ERROR_END_OF_STREAM;
    }

    const int64_t now_us = base::TimeMicros();
    if (stream.next_playlist_refresh_time_us == 0 ||
        now_us >= stream.next_playlist_refresh_time_us) {
      status_t err = RefreshStreamPlaylistLocked(stream, false);
      if (err != OK) {
        return err;
      }
    }

    if (stream.next_segment_sequence >=
        stream.playlist.media_sequence +
            static_cast<int32_t>(stream.playlist.segments.size())) {
      return WOULD_BLOCK;
    }
  }

  const auto index = static_cast<size_t>(stream.next_segment_sequence -
                                         stream.playlist.media_sequence);
  if (index >= stream.playlist.segments.size()) {
    return WOULD_BLOCK;
  }

  const auto& segment = stream.playlist.segments[index];
  AVE_LOG(LS_INFO) << "HttpLiveSource loading segment: sequence="
                   << segment.sequence << " uri=" << segment.uri
                   << " start_us=" << stream.next_segment_start_time_us
                   << " duration_us=" << segment.duration_us;
  const int32_t next_segment_sequence = stream.next_segment_sequence;
  const int64_t next_segment_start_time_us = stream.next_segment_start_time_us;
  stream.next_segment_sequence = segment.sequence + 1;
  stream.next_segment_start_time_us += segment.duration_us;

  status_t err = LoadSegmentLocked(stream, segment, next_segment_start_time_us);
  if (err != OK) {
    stream.next_segment_sequence = next_segment_sequence;
    stream.next_segment_start_time_us = next_segment_start_time_us;
    return err;
  }
  return OK;
}

status_t HttpLiveSource::LoadSegmentLocked(
    StreamState& stream,
    const http_live::MediaPlaylistSegment& segment,
    int64_t segment_start_time_us) {
  if (UsesFragmentedMp4(stream.playlist)) {
    return LoadFragmentedSegmentLocked(stream, segment, segment_start_time_us);
  }

  std::vector<uint8_t> bytes;
  status_t err =
      segment.byterange_length > 0
          ? FetchUrlRangeLocked(segment.uri,
                                static_cast<off64_t>(segment.byterange_offset),
                                segment.byterange_length, bytes)
          : FetchUrlLocked(segment.uri, bytes);
  if (err != OK) {
    return err;
  }

  auto data_source =
      std::make_shared<MemoryDataSource>(segment.uri, std::move(bytes));
  return LoadTsSegmentLocked(data_source, segment_start_time_us);
}

status_t HttpLiveSource::LoadFragmentedSegmentLocked(
    StreamState& stream,
    const http_live::MediaPlaylistSegment& segment,
    int64_t segment_start_time_us) {
  if (!demuxer_factory_) {
    return media::ERROR_UNSUPPORTED;
  }
  if (stream.playlist.init_segment.uri.empty()) {
    return media::ERROR_UNSUPPORTED;
  }

  if (stream.init_segment_bytes.empty()) {
    status_t err = stream.playlist.init_segment.byterange_length > 0
                       ? FetchUrlRangeLocked(
                             stream.playlist.init_segment.uri,
                             static_cast<off64_t>(
                                 stream.playlist.init_segment.byterange_offset),
                             stream.playlist.init_segment.byterange_length,
                             stream.init_segment_bytes)
                       : FetchUrlLocked(stream.playlist.init_segment.uri,
                                        stream.init_segment_bytes);
    if (err != OK) {
      return err;
    }
  }

  std::vector<uint8_t> fragment_bytes;
  status_t err =
      segment.byterange_length > 0
          ? FetchUrlRangeLocked(segment.uri,
                                static_cast<off64_t>(segment.byterange_offset),
                                segment.byterange_length, fragment_bytes)
          : FetchUrlLocked(segment.uri, fragment_bytes);
  if (err != OK) {
    return err;
  }

  std::vector<uint8_t> bytes;
  bytes.reserve(stream.init_segment_bytes.size() + fragment_bytes.size());
  bytes.insert(bytes.end(), stream.init_segment_bytes.begin(),
               stream.init_segment_bytes.end());
  bytes.insert(bytes.end(), fragment_bytes.begin(), fragment_bytes.end());

  auto data_source =
      std::make_shared<MemoryDataSource>(segment.uri, std::move(bytes));
  return LoadDemuxedSegmentLocked(data_source, segment_start_time_us);
}

status_t HttpLiveSource::LoadTsSegmentLocked(
    const std::shared_ptr<DataSource>& data_source,
    int64_t segment_start_time_us) {
  TsSegmentExtractor extractor(data_source);
  status_t err = extractor.Parse();
  if (err != OK) {
    return err;
  }

  for (MediaType media_type : {MediaType::AUDIO, MediaType::VIDEO}) {
    auto source = extractor.GetSource(media_type);
    if (!source) {
      continue;
    }

    std::shared_ptr<media::MediaMeta> format = source->GetFormat();
    if (!format) {
      continue;
    }

    err = EnsureTrackStateLocked(media_type, format);
    if (err != OK) {
      return err;
    }

    err = QueueTsPacketsLocked(media_type, source, segment_start_time_us);
    if (err != OK) {
      return err;
    }
  }

  return OK;
}

status_t HttpLiveSource::LoadDemuxedSegmentLocked(
    const std::shared_ptr<DataSource>& data_source,
    int64_t segment_start_time_us) {
  auto demuxer = demuxer_factory_
                     ? demuxer_factory_->CreateDemuxer(data_source, {})
                     : nullptr;
  if (!demuxer) {
    return media::ERROR_UNSUPPORTED;
  }

  bool queued_track = false;
  const size_t track_count = demuxer->GetTrackCount();
  for (size_t i = 0; i < track_count; ++i) {
    auto source = demuxer->GetTrack(i);
    if (!source) {
      continue;
    }

    std::shared_ptr<media::MediaMeta> format = source->GetFormat();
    if (!format) {
      demuxer->GetTrackFormat(format, i);
    }
    if (!format) {
      continue;
    }

    const MediaType media_type = format->stream_type();
    if (media_type != MediaType::AUDIO && media_type != MediaType::VIDEO) {
      continue;
    }

    status_t err = source->Start(nullptr);
    if (err != OK) {
      return err;
    }

    err = EnsureTrackStateLocked(media_type, format);
    if (err == OK) {
      err =
          QueueDemuxedPacketsLocked(media_type, source, segment_start_time_us);
    }

    source->Stop();
    if (err != OK) {
      return err;
    }
    queued_track = true;
  }

  return queued_track ? static_cast<status_t>(OK)
                      : static_cast<status_t>(media::ERROR_UNSUPPORTED);
}

status_t HttpLiveSource::FetchUrlLocked(const std::string& url,
                                        std::vector<uint8_t>& data) {
  return FetchUrlRangeLocked(url, 0, -1, data);
}

status_t HttpLiveSource::FetchUrlRangeLocked(const std::string& url,
                                             off64_t offset,
                                             int64_t length,
                                             std::vector<uint8_t>& data) {
  if (!http_provider_) {
    return NO_INIT;
  }
  auto connection = http_provider_->CreateConnection();
  if (!connection || !connection->Connect(url.c_str(), headers_)) {
    return UNKNOWN_ERROR;
  }

  if (length > 0) {
    data.reserve(static_cast<size_t>(length));
  } else {
    const off64_t size = connection->GetSize();
    if (size > offset) {
      data.reserve(static_cast<size_t>(size - offset));
    }
  }

  data.clear();
  std::array<uint8_t, 32 * 1024> chunk{};
  off64_t read_offset = offset;
  int64_t remaining = length;
  for (;;) {
    size_t request_size = chunk.size();
    if (remaining == 0) {
      break;
    }
    if (remaining > 0) {
      request_size =
          std::min<size_t>(request_size, static_cast<size_t>(remaining));
    }

    ssize_t bytes_read =
        connection->ReadAt(read_offset, chunk.data(), request_size);
    if (bytes_read < 0) {
      connection->Disconnect();
      return UNKNOWN_ERROR;
    }
    if (bytes_read == 0) {
      break;
    }
    data.insert(data.end(), chunk.begin(), chunk.begin() + bytes_read);
    read_offset += bytes_read;
    if (remaining > 0) {
      remaining -= bytes_read;
    }
  }
  connection->Disconnect();

  if (length > 0 && std::cmp_less(data.size(), length)) {
    AVE_LOG(LS_ERROR) << "HttpLiveSource short read: url=" << url
                      << " offset=" << offset << " length=" << length
                      << " actual=" << data.size();
    return UNKNOWN_ERROR;
  }
  return OK;
}

status_t HttpLiveSource::FetchTextLocked(const std::string& url,
                                         std::string& text) {
  std::vector<uint8_t> bytes;
  status_t err = FetchUrlLocked(url, bytes);
  if (err != OK) {
    return err;
  }
  text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return OK;
}

status_t HttpLiveSource::QueueTsPacketsLocked(
    MediaType media_type,
    const std::shared_ptr<media::mpeg2ts::PacketSource>& source,
    int64_t segment_start_time_us) {
  TrackState* track = FindTrackLocked(media_type);
  if (!track || !track->packet_source) {
    return UNKNOWN_ERROR;
  }

  size_t packet_count = 0;
  for (;;) {
    std::shared_ptr<media::MediaFrame> packet;
    status_t err = source->DequeueAccessUnit(packet);
    if (err == media::INFO_DISCONTINUITY) {
      continue;
    }
    if (err == media::ERROR_END_OF_STREAM) {
      AVE_LOG(LS_INFO) << "HttpLiveSource queued " << packet_count
                       << " packets for "
                       << (media_type == MediaType::AUDIO ? "audio" : "video")
                       << " at segment_start_us=" << segment_start_time_us;
      return OK;
    }
    if (err != OK) {
      return err;
    }

    if (!packet) {
      continue;
    }

    OffsetFrameTimestamp(packet, segment_start_time_us);
    track->packet_source->QueueAccessunit(packet);
    ++packet_count;
  }
}

status_t HttpLiveSource::QueueDemuxedPacketsLocked(
    MediaType media_type,
    const std::shared_ptr<media::MediaSource>& source,
    int64_t segment_start_time_us) NO_THREAD_SAFETY_ANALYSIS {
  TrackState* track = FindTrackLocked(media_type);
  if (!track || !track->packet_source || !source) {
    return UNKNOWN_ERROR;
  }

  size_t packet_count = 0;
  for (;;) {
    std::shared_ptr<media::MediaFrame> packet;
    lock_.unlock();
    status_t err = source->Read(packet, nullptr);
    lock_.lock();

    if (err == media::INFO_FORMAT_CHANGED) {
      auto format = source->GetFormat();
      if (format) {
        err = EnsureTrackStateLocked(media_type, format);
      } else {
        err = OK;
      }
      if (err != OK) {
        return err;
      }
      continue;
    }
    if (err == media::ERROR_END_OF_STREAM) {
      AVE_LOG(LS_INFO) << "HttpLiveSource queued " << packet_count
                       << " demuxed packets for "
                       << (media_type == MediaType::AUDIO ? "audio" : "video")
                       << " at segment_start_us=" << segment_start_time_us;
      return OK;
    }
    if (err != OK) {
      return err;
    }
    if (!packet) {
      continue;
    }

    OffsetFrameTimestamp(packet, segment_start_time_us);
    track->packet_source->QueueAccessunit(packet);
    ++packet_count;
  }
}

status_t HttpLiveSource::EnsureTrackStateLocked(
    MediaType media_type,
    const std::shared_ptr<media::MediaMeta>& format) {
  TrackState* track = FindTrackLocked(media_type);
  if (!track) {
    TrackState state;
    state.index = tracks_.size();
    state.media_type = media_type;
    state.format = CloneMeta(format);
    if (state.format) {
      state.format->SetDuration(base::TimeDelta::Micros(duration_us_));
    }
    state.packet_source = std::make_shared<PacketSource>(state.format);
    tracks_.push_back(std::move(state));
    return OK;
  }

  if (format) {
    track->format = CloneMeta(format);
  }
  if (track->format) {
    track->format->SetDuration(base::TimeDelta::Micros(duration_us_));
  }
  if (track->packet_source) {
    track->packet_source->SetFormat(track->format);
  }
  return OK;
}

HttpLiveSource::TrackState* HttpLiveSource::FindTrackLocked(
    MediaType media_type) {
  for (auto& track : tracks_) {
    if (track.media_type == media_type) {
      return &track;
    }
  }
  return nullptr;
}

const HttpLiveSource::TrackState* HttpLiveSource::FindTrackLocked(
    MediaType media_type) const {
  for (const auto& track : tracks_) {
    if (track.media_type == media_type) {
      return &track;
    }
  }
  return nullptr;
}

status_t HttpLiveSource::FinishPrepareLocked() {
  if (tracks_.empty()) {
    return UNKNOWN_ERROR;
  }
  prepared_ = true;
  return OK;
}

void HttpLiveSource::ResetLocked() {
  master_url_.clear();
  headers_.clear();
  playlist_ = http_live::Playlist();
  source_format_.reset();
  tracks_.clear();
  primary_stream_ = StreamState();
  audio_stream_ = StreamState();
  duration_us_ = -1;
  prepared_ = false;
  started_ = false;
  end_of_stream_ = false;
}

void HttpLiveSource::OffsetFrameTimestamp(
    const std::shared_ptr<media::MediaFrame>& frame,
    int64_t offset_us) {
  if (!frame) {
    return;
  }
  if (frame->stream_type() == MediaType::AUDIO) {
    auto* info = frame->audio_info();
    if (info) {
      if (info->pts.IsFinite()) {
        info->pts = base::Timestamp::Micros(info->pts.us() + offset_us);
        frame->SetPts(info->pts);
      }
      if (info->dts.IsFinite()) {
        info->dts = base::Timestamp::Micros(info->dts.us() + offset_us);
        frame->SetDts(info->dts);
      }
    }
  } else if (frame->stream_type() == MediaType::VIDEO) {
    auto* info = frame->video_info();
    if (info) {
      if (info->pts.IsFinite()) {
        info->pts = base::Timestamp::Micros(info->pts.us() + offset_us);
        frame->SetPts(info->pts);
      }
      if (info->dts.IsFinite()) {
        info->dts = base::Timestamp::Micros(info->dts.us() + offset_us);
        frame->SetDts(info->dts);
      }
    }
  }
}

}  // namespace player
}  // namespace ave
