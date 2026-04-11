/*
 * mpeg2_ts_demuxer.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "demuxer/mpeg2/mpeg2_ts_demuxer.h"

#include "base/logging.h"
#include "demuxer/mpeg2/mpeg2_common.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_mimes.h"
#include "media/modules/mpeg2ts/ts_parser.h"

namespace ave {
namespace player {

namespace {

constexpr size_t kTsPacketSize = 188;
constexpr size_t kMaxInitPackets = 32768;
constexpr size_t kMaxTsPacketSpacing = 204;
constexpr size_t kResyncProbeSize = kTsPacketSize + 512;
constexpr size_t kResyncAdvance = 256;

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

bool IsTrackFormatReady(const std::shared_ptr<media::MediaMeta>& format) {
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

}  // namespace

Mpeg2TsDemuxer::Mpeg2TsDemuxer(std::shared_ptr<ave::DataSource> data_source)
    : Demuxer(std::move(data_source)) {}

Mpeg2TsDemuxer::~Mpeg2TsDemuxer() = default;

const char* Mpeg2TsDemuxer::name() {
  return "Mpeg2TsDemuxer";
}

status_t Mpeg2TsDemuxer::Init() {
  if (initialized_) {
    return OK;
  }

  mpeg2::TsPacketLayout layout;
  if (!mpeg2::SniffMpeg2Ts(data_source_, &layout)) {
    return media::ERROR_UNSUPPORTED;
  }
  sync_offset_ = layout.sync_offset;
  packet_stride_ = layout.packet_stride;
  offset_ = static_cast<off64_t>(sync_offset_);

  (void)data_source_->GetSize(&size_);

  source_format_ = media::MediaMeta::CreatePtr(
      media::MediaType::UNKNOWN, media::MediaMeta::FormatType::kTrack);
  source_format_->SetMime(media::MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

  parser_ = std::make_unique<media::mpeg2ts::TSParser>();

  status_t last_err = OK;
  for (size_t i = 0; i < kMaxInitPackets; ++i) {
    last_err = FeedMore();
    MaybeAddTrack(media::mpeg2ts::TSParser::VIDEO);
    MaybeAddTrack(media::mpeg2ts::TSParser::AUDIO);

    if (HasTrack(media::mpeg2ts::TSParser::VIDEO) &&
        HasTrack(media::mpeg2ts::TSParser::AUDIO)) {
      break;
    }

    if (last_err != OK) {
      break;
    }
  }

  if (tracks_.empty()) {
    return last_err == OK ? media::ERROR_UNSUPPORTED : last_err;
  }

  initialized_ = true;
  return OK;
}

status_t Mpeg2TsDemuxer::GetFormat(std::shared_ptr<media::MediaMeta>& format) {
  if (!initialized_) {
    return NO_INIT;
  }
  format = source_format_;
  return OK;
}

size_t Mpeg2TsDemuxer::GetTrackCount() {
  return tracks_.size();
}

status_t Mpeg2TsDemuxer::GetTrackFormat(
    std::shared_ptr<media::MediaMeta>& format,
    size_t track_index) {
  if (!initialized_) {
    return NO_INIT;
  }
  if (track_index >= tracks_.size()) {
    return BAD_VALUE;
  }

  format = tracks_[track_index].packet_source->GetFormat();
  return format != nullptr ? OK : NO_INIT;
}

std::shared_ptr<media::MediaSource> Mpeg2TsDemuxer::GetTrack(
    size_t track_index) {
  if (!initialized_ || track_index >= tracks_.size()) {
    return nullptr;
  }

  auto packet_source = tracks_[track_index].packet_source;
  auto self = shared_from_this();
  return std::make_shared<mpeg2::PacketSourceTrack>(
      packet_source, [self, packet_source]() {
        return self->FeedUntilBufferAvailable(packet_source);
      });
}

bool Mpeg2TsDemuxer::HasTrack(unsigned type) const {
  for (const auto& track : tracks_) {
    if (track.source_type == type) {
      return true;
    }
  }
  return false;
}

void Mpeg2TsDemuxer::MaybeAddTrack(unsigned type) {
  if (HasTrack(type)) {
    return;
  }

  auto source = parser_->GetSource(
      static_cast<media::mpeg2ts::TSParser::SourceType>(type));
  if (!source) {
    return;
  }

  auto format = source->GetFormat();
  if (!IsTrackFormatReady(format)) {
    if (format) {
      AVE_LOG(LS_INFO) << "Mpeg2TsDemuxer track not ready: type=" << type
                       << " mime=" << format->mime()
                       << " width=" << format->width()
                       << " height=" << format->height()
                       << " sample_rate=" << format->sample_rate()
                       << " has_private_data="
                       << static_cast<bool>(format->private_data());
    }
    return;
  }

  AVE_LOG(LS_INFO) << "Mpeg2TsDemuxer add track: type=" << type
                   << " mime=" << format->mime();
  tracks_.push_back(TrackEntry{
      type,
      source,
  });
}

status_t Mpeg2TsDemuxer::FeedMore() {
  if (eos_signaled_) {
    return media::ERROR_END_OF_STREAM;
  }

  uint8_t probe[kResyncProbeSize];
  uint8_t packet[kTsPacketSize];

  for (;;) {
    ssize_t probe_read = data_source_->ReadAt(offset_, probe, sizeof(probe));
    if (probe_read < static_cast<ssize_t>(kTsPacketSize)) {
      status_t final_result = probe_read < 0 ? static_cast<status_t>(probe_read)
                                             : media::ERROR_END_OF_STREAM;
      if (!eos_signaled_) {
        parser_->SignalEOS(final_result);
        eos_signaled_ = true;
      }
      return final_result;
    }

    bool found_candidate = false;
    const size_t limit = static_cast<size_t>(probe_read) - kTsPacketSize + 1;
    for (size_t candidate = 0; candidate < limit; ++candidate) {
      if (!LooksLikePacketStart(probe, static_cast<size_t>(probe_read),
                                candidate)) {
        continue;
      }

      const off64_t packet_offset = offset_ + static_cast<off64_t>(candidate);
      ssize_t bytes_read =
          data_source_->ReadAt(packet_offset, packet, sizeof(packet));
      if (bytes_read < static_cast<ssize_t>(sizeof(packet))) {
        status_t final_result = bytes_read < 0
                                    ? static_cast<status_t>(bytes_read)
                                    : media::ERROR_END_OF_STREAM;
        if (!eos_signaled_) {
          parser_->SignalEOS(final_result);
          eos_signaled_ = true;
        }
        return final_result;
      }

      media::mpeg2ts::TSParser::SyncEvent event(packet_offset);
      status_t err = parser_->FeedTSPacket(packet, sizeof(packet), &event);
      if (err == media::ERROR_MALFORMED) {
        offset_ = packet_offset + 1;
        found_candidate = true;
        break;
      }

      offset_ = packet_offset + static_cast<off64_t>(sizeof(packet));
      MaybeAddTrack(media::mpeg2ts::TSParser::VIDEO);
      MaybeAddTrack(media::mpeg2ts::TSParser::AUDIO);

      if (err != OK && !eos_signaled_) {
        parser_->SignalEOS(err);
        eos_signaled_ = true;
      }

      return err;
    }

    if (found_candidate) {
      continue;
    }

    if (probe_read < static_cast<ssize_t>(sizeof(probe))) {
      if (!eos_signaled_) {
        parser_->SignalEOS(media::ERROR_END_OF_STREAM);
        eos_signaled_ = true;
      }
      return media::ERROR_END_OF_STREAM;
    }

    offset_ += kResyncAdvance;
  }
}

status_t Mpeg2TsDemuxer::FeedUntilBufferAvailable(
    const std::shared_ptr<media::mpeg2ts::PacketSource>& source) {
  for (;;) {
    status_t final_result = OK;
    if (source->HasBufferAvailable(&final_result)) {
      return OK;
    }

    if (final_result != OK) {
      return final_result;
    }

    status_t err = FeedMore();
    if (err != OK && err != media::ERROR_END_OF_STREAM) {
      return err;
    }
  }
}

}  // namespace player
}  // namespace ave
