/*
 * generic_source.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "generic_source.h"

#include <memory>
#include <string>

#include "api/content_source.h"
#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"

#include "base/data_source/file_source.h"

#include "media/foundation/looper.h"
#include "media/foundation/media_source.h"
#include "media/foundation/media_utils.h"
#include "media/foundation/message.h"

namespace avp {

namespace {
const int32_t kDefaultPollBufferingIntervalUs = 1000000;
}  // namespace

using ave::DataSource;
using ave::FileSource;
using ave::media::Looper;
using ave::media::MediaSource;
using ave::media::ReplyToken;

GenericSource::GenericSource()
    : fd_(-1),
      offset_(-1),
      length_(-1),
      duration_us_(-1LL),
      bitrate_(-1LL),
      audio_last_dequeue_time_us_(-1),
      video_last_dequeue_time_us_(-1),
      pending_read_buffer_types_(0),
      preparing_(false),
      started_(false),
      is_streaming_(false) {}

GenericSource::~GenericSource() = default;

void GenericSource::SetNotify(std::shared_ptr<Notify> notify) {
  std::lock_guard<std::mutex> lock(lock_);
  notify_ = notify;
}

void GenericSource::ResetDataSource() {
  uri_.clear();
  offset_ = -1;
  length_ = -1;
  duration_us_ = -1;
  bitrate_ = -1;

  demuxer_.reset();
  sources_.clear();

  audio_last_dequeue_time_us_ = -1;
  video_last_dequeue_time_us_ = -1;
  pending_read_buffer_types_ = 0;

  preparing_ = false;
  started_ = false;
  is_streaming_ = false;
}

// TODO: implement http source
status_t GenericSource::SetDataSource(const char* url) {
  std::lock_guard<std::mutex> lock(lock_);
  ResetDataSource();

  uri_ = url;
  return 0;
}

status_t GenericSource::SetDataSource(int fd, int64_t offset, int64_t length) {
  std::lock_guard<std::mutex> lock(lock_);
  AVE_LOG(LS_VERBOSE) << "SetDataSource fd: " << fd << ", offset: " << offset
                      << ", length: " << length;
  ResetDataSource();

  fd_.reset(dup(fd));
  offset_ = offset;
  length_ = length;

  return 0;
}

status_t GenericSource::SetDataSource(std::shared_ptr<DataSource> data_source) {
  std::lock_guard<std::mutex> lock(lock_);
  ResetDataSource();
  data_source_ = std::move(data_source);
  return 0;
}

void GenericSource::Prepare() {
  std::lock_guard<std::mutex> lock(lock_);
  if (looper_ == nullptr) {
    looper_ = std::make_shared<Looper>();
    looper_->setName("GenericSource");
    looper_->registerHandler(shared_from_this());
    looper_->start();
  }

  auto message = std::make_shared<Message>(kWhatPrepare, shared_from_this());
  message->post();
}

void GenericSource::Start() {
  std::lock_guard<std::mutex> lock(lock_);

  if (audio_track_.source != nullptr) {
    PostReadBuffer(MediaType::AUDIO);
  }

  if (video_track_.source != nullptr) {
    PostReadBuffer(MediaType::VIDEO);
  }

  started_ = true;
}

void GenericSource::Stop() {
  std::lock_guard<std::mutex> lock(lock_);
  started_ = false;
}

void GenericSource::Pause() {
  std::lock_guard<std::mutex> lock(lock_);
  started_ = false;
}

void GenericSource::Resume() {
  std::lock_guard<std::mutex> lock(lock_);
  started_ = true;
}

status_t GenericSource::SeekTo(int64_t seek_time_us, SeekMode mode) {
  AVE_LOG(LS_VERBOSE) << "SeekTo: " << seek_time_us << ", mode: " << mode;
  auto message = std::make_shared<Message>(kWhatSeek, shared_from_this());
  message->setInt64("seek_time_us", seek_time_us);
  message->setInt32("mode", mode);

  std::shared_ptr<Message> response;
  status_t err = message->postAndWaitResponse(response);
  if (err == ave::OK && response != nullptr) {
    AVE_CHECK(response->findInt32("err", &err));
  }
  return err;
}

std::shared_ptr<MediaFormat> GenericSource::GetFormat() {
  std::lock_guard<std::mutex> lock(lock_);
  return source_format_;
}

std::shared_ptr<MediaFormat> GenericSource::GetTrackInfo(
    size_t track_index) const {
  std::lock_guard<std::mutex> lock(lock_);
  AVE_DCHECK(track_index < sources_.size());
  if (track_index >= sources_.size()) {
    return nullptr;
  }

  return sources_[track_index]->GetFormat();
}

status_t GenericSource::DequeueAccessUnit(
    MediaType track_type,
    std::shared_ptr<MediaPacket>& access_unit) {
  std::lock_guard<std::mutex> lock(lock_);

  if (!started_) {
    return ave::WOULD_BLOCK;
  }

  auto& track = track_type == MediaType::VIDEO ? video_track_ : audio_track_;

  if (track.source == nullptr) {
    return ave::WOULD_BLOCK;
  }

  status_t result = ave::OK;

  if (!track.packet_source->HasBufferAvailable(&result)) {
    if (result == ave::OK) {
      PostReadBuffer(track_type);
      return ave::WOULD_BLOCK;
    }
    return result;
  }

  result = track.packet_source->DequeueAccessUnit(access_unit);

  // TODO(youfa) judge to read more data here.

  if (result != ave::OK) {
    return result;
  }

  int64_t time_us = 0;
  if (access_unit->media_type() == MediaType::VIDEO) {
    time_us = access_unit->video_info()->pts.us();
    video_last_dequeue_time_us_ = time_us;
  } else {
    time_us = access_unit->audio_info()->pts.us();
    audio_last_dequeue_time_us_ = time_us;
  }

  // TODO(youfa) fetch subtitle data with timeUs
  if (subtitle_track_.source != nullptr) {
    auto msg =
        std::make_shared<Message>(kWhatFetchSubtitleData, shared_from_this());
    msg->setInt64("time_us", time_us);
    msg->post();
  }

  if (timed_text_track_.source != nullptr) {
    auto msg =
        std::make_shared<Message>(kWhatFetchSubtitleData, shared_from_this());
    msg->setInt64("time_us", time_us);
    msg->post();
  }

  return result;
}

status_t GenericSource::GetDuration(int64_t* duration_us) {
  std::lock_guard<std::mutex> lock(lock_);
  *duration_us = duration_us_;
  return ave::OK;
}

size_t GenericSource::GetTrackCount() const {
  std::lock_guard<std::mutex> lock(lock_);
  return sources_.size();
}

status_t GenericSource::SelectTrack(size_t track_index, bool select) {
  std::lock_guard<std::mutex> lock(lock_);
  if (track_index >= sources_.size()) {
    return ave::INVALID_OPERATION;
  }

  if (!select) {
    // only subtitile and timed text track can be deselected
    Track* track = nullptr;
    if (subtitle_track_.source != nullptr &&
        subtitle_track_.index == track_index) {
      track = &subtitle_track_;
    } else if (timed_text_track_.source != nullptr &&
               timed_text_track_.index == track_index) {
      track = &timed_text_track_;
    }
    if (track != nullptr) {
      AVE_LOG(LS_ERROR) << "Cannot deselect track " << track;
      return ave::INVALID_OPERATION;
    }

    track->source->Stop();
    track->source.reset();
    track->packet_source->Clear();
  }

  const auto source = sources_[track_index];
  auto meta = source->GetFormat();
  auto stream_type = meta->stream_type();

  switch (stream_type) {
    case MediaType::AUDIO:
    case MediaType::VIDEO: {
      Track* track =
          stream_type == MediaType::AUDIO ? &audio_track_ : &video_track_;

      if (track->source != nullptr && track->index == track_index) {
        return ave::OK;
      }

      auto msg =
          std::make_shared<Message>(kWhatChangeAVSource, shared_from_this());
      msg->setInt32("track_index", static_cast<int32_t>(track_index));
      msg->post();
      return ave::OK;
    }

    case MediaType::SUBTITLE:
    case MediaType::TIMED_TEXT: {
      Track* track = stream_type == MediaType::SUBTITLE ? &subtitle_track_
                                                        : &timed_text_track_;
      if (track->source != nullptr && track->index == track_index) {
        return ave::OK;
      }

      track->index = track_index;
      if (track->source != nullptr) {
        track->source->Stop();
        track->source.reset();
        track->packet_source->Clear();
      }

      track->source = sources_[track_index];
      track->source->Start(nullptr);
      if (track->packet_source == nullptr) {
        track->packet_source = std::make_shared<PacketSource>(meta);
      } else {
        track->packet_source->SetFormat(meta);
      }

      status_t ret = ave::OK;

      if (subtitle_track_.source != nullptr &&
          !subtitle_track_.packet_source->HasBufferAvailable(&ret)) {
        auto msg = std::make_shared<Message>(kWhatFetchSubtitleData,
                                             shared_from_this());
        msg->post();
      }

      if (timed_text_track_.source != nullptr &&
          !timed_text_track_.packet_source->HasBufferAvailable(&ret)) {
        auto msg = std::make_shared<Message>(kWhatFetchSubtitleData,
                                             shared_from_this());
        msg->post();
      }

      return ave::OK;
    }
    default: {
      return ave::INVALID_OPERATION;
    }
  }
}

/***************************************/
status_t GenericSource::InitFromDataSource() {
  AVE_LOG(LS_INFO) << "GenericSource::initFrodata_source_";

  std::shared_ptr<DataSource> datasource;
  datasource = data_source_;

  AVE_CHECK(datasource != nullptr);

  lock_.unlock();
  demuxer_ = demuxer_factory_->CreateDemuxer(datasource);

  if (demuxer_ == nullptr) {
    lock_.lock();
    return ave::UNKNOWN_ERROR;
  }

  std::shared_ptr<MediaFormat> source_format;

  demuxer_->GetFormat(source_format);

  size_t num_tracks = demuxer_->GetTrackCount();
  if (num_tracks == 0) {
    AVE_LOG(LS_ERROR) << "InitFrodata_source_, source has no track!";
    lock_.lock();
    return ave::UNKNOWN_ERROR;
  }
  lock_.lock();

  source_format_ = source_format;

  if (source_format_ != nullptr) {
    duration_us_ = source_format_->duration().us_or(-1);
  }

  int64_t total_bitrate = 0;

  for (size_t i = 0; i < num_tracks; i++) {
    auto source = demuxer_->GetTrack(i);
    if (source == nullptr) {
      continue;
    }

    std::shared_ptr<MediaFormat> format;
    demuxer_->GetTrackFormat(format, i);

    if (format == nullptr) {
      AVE_LOG(LS_ERROR) << "no metadata for track " << i;
      return ave::UNKNOWN_ERROR;
    }
    sources_.push_back(source);

    Track* track = nullptr;

    if (format->stream_type() == MediaType::AUDIO) {
      track = &audio_track_;
    } else if (format->stream_type() == MediaType::VIDEO) {
      track = &video_track_;
    }

    if (track != nullptr) {
      track->index = i;
      track->source = source;
      track->packet_source = std::make_shared<PacketSource>(format);
    }

    AVE_LOG(LS_VERBOSE) << "InitFrodata_source_ track[" << i << "]: ";

    auto duration_us = format->duration().us_or(-1);
    if (duration_us > duration_us_) {
      duration_us_ = duration_us;
    }

    auto bitrate = format->bitrate();
    if (bitrate >= 0) {
      total_bitrate += bitrate;
    }
  }

  bitrate_ = total_bitrate;

  AVE_LOG(LS_VERBOSE) << "initFrodata_source_ done. tracks.size: "
                      << sources_.size();

  if (sources_.size() == 0) {
    return ave::UNKNOWN_ERROR;
  }

  return ave::OK;
}

void GenericSource::OnPrepare() {
  AVE_LOG(LS_VERBOSE) << "OnPrepare, data_source_:" << data_source_;
  // create DataSource
  if (data_source_ == nullptr) {
    if (!uri_.empty()) {
      const char* uri = uri_.c_str();
      // AVE_LOG(LS_INFO) << "onPrepare, uri (" << uri << ")";
      if (!strncasecmp("http://", uri, 7) || !strncasecmp("https://", uri, 8)) {
        // TODO: create http source
      }
    } else {
      auto file_source =
          std::make_shared<FileSource>(dup(fd_.get()), offset_, length_);
      if (file_source->InitCheck() == ave::OK) {
        data_source_ = std::move(file_source);
      }
    }

    if (data_source_ == nullptr) {
      AVE_LOG(LS_ERROR) << "Failed to create DataSource";
      NotifyPreparedAndCleanup(ave::UNKNOWN_ERROR);
      return;
    }
  }
  // TODO: if streaming, wrap data source with cache source

  // probe source and init demuxer
  status_t err = InitFromDataSource();
  if (err != ave::OK) {
    NotifyPreparedAndCleanup(err);
    return;
  }

  if (video_track_.source != nullptr) {
    auto format = video_track_.source->GetFormat();
    NotifyVideoSizeChanged(format);
  }

  // FIXME: how to confirm can seek ?
  NotifyFlagsChanged(FLAG_CAN_PAUSE | FLAG_CAN_SEEK | FLAG_CAN_SEEK_BACKWARD |
                     FLAG_CAN_SEEK_FORWARD);

  FinishPrepare();

  AVE_LOG(LS_VERBOSE) << "OnPrepare Done";
}

void GenericSource::FinishPrepare() {
  AVE_LOG(LS_VERBOSE) << "FinishPrepare";
  status_t err = StartSources();
  if (err != ave::OK) {
    NotifyPreparedAndCleanup(err);
    return;
  }

  if (is_streaming_) {
    // TODO: (yofua) when streaming, notify until buffering done
  } else {
    NotifyPrepared();
  }

  if (video_track_.source != nullptr) {
    PostReadBuffer(MediaType::VIDEO);
  }

  if (audio_track_.source != nullptr) {
    PostReadBuffer(MediaType::AUDIO);
  }
}

status_t GenericSource::StartSources() {
  if (video_track_.source != nullptr) {
    status_t err = video_track_.source->Start(nullptr);
    if (err != ave::OK) {
      AVE_LOG(LS_ERROR) << "Failed to start video source";
      return err;
    }
  }

  if (audio_track_.source != nullptr) {
    status_t err = audio_track_.source->Start(nullptr);
    if (err != ave::OK) {
      AVE_LOG(LS_ERROR) << "Failed to start audio source";
      return err;
    }
  }

  return ave::OK;
}

void GenericSource::NotifyPreparedAndCleanup(status_t err) {
  if (err != ave::OK) {
    // TODO(youfa) reset data source
    ResetDataSource();
  }
  NotifyPrepared(err);
}

void GenericSource::SchedulePollBuffering() {
  auto msg = std::make_shared<Message>(kWhatPollBuffering, shared_from_this());
  msg->post(kDefaultPollBufferingIntervalUs);
}

void GenericSource::OnPollBuffering() {
  // TODO: finish buffering and notify prepared

  SchedulePollBuffering();
}

void GenericSource::PostReadBuffer(MediaType track_type) {
  if ((pending_read_buffer_types_ & (1 << static_cast<uint32_t>(track_type))) ==
      0) {
    pending_read_buffer_types_ |= (1 << static_cast<uint32_t>(track_type));
    auto message =
        std::make_shared<Message>(kWhatReadBuffer, shared_from_this());
    message->setInt32("track_type", static_cast<int32_t>(track_type));
    message->post();
  }
}

void GenericSource::OnReadBuffer(const std::shared_ptr<Message>& message) {
  int32_t type = 0;
  AVE_CHECK(message->findInt32("track_type", &type));
  // AVE_DCHECK(type >= 0 && type < static_cast<int32_t>(MediaType::MAX));
  auto track_type = static_cast<MediaType>(type);
  pending_read_buffer_types_ &= ~(1 << type);
  ReadBuffer(track_type);
}

void GenericSource::ReadBuffer(MediaType track_type,
                               int64_t seek_time_us,
                               SeekMode seek_mode,
                               int64_t* actual_time_us) {
  size_t max_buffers = 1;
  Track* track = nullptr;
  switch (track_type) {
    case MediaType::VIDEO:
      max_buffers = 8;
      track = &video_track_;
      break;
    case MediaType::AUDIO:
      max_buffers = 64;
      track = &audio_track_;
      break;
    case MediaType::SUBTITLE:
      track = &subtitle_track_;
      break;
    case MediaType::TIMED_TEXT:
      track = &timed_text_track_;
      break;
    default:
      break;
  }

  if (track->source == nullptr) {
    return;
  }

  if (actual_time_us != nullptr) {
    *actual_time_us = seek_time_us;
  }

  MediaSource::ReadOptions read_options;

  if (seek_time_us >= 0) {
    read_options.SetSeekTo(
        seek_time_us,
        static_cast<MediaSource::ReadOptions::SeekMode>(seek_mode));
  }

  const bool could_read_multiple = track->source->SupportReadMultiple();
  if (could_read_multiple) {
    read_options.SetNonBlocking();
  }

  for (size_t num_buffer = 0; num_buffer < max_buffers;) {
    std::vector<std::shared_ptr<MediaPacket>> media_packets;
    status_t err = ave::OK;

    // will unlock later, add reference
    auto& source = track->source;

    //    AVE_LOG(LS_INFO) << "before read type:" << trackType;
    lock_.unlock();
    if (could_read_multiple) {
      err = source->ReadMultiple(media_packets, max_buffers - num_buffer,
                                 &read_options);
    } else {
      std::shared_ptr<MediaPacket> packet;
      err = source->Read(packet, &read_options);
      if (err == ave::OK && packet != nullptr) {
        media_packets.push_back(packet);
      }
    }
    lock_.lock();
    //    AVE_LOG(LS_INFO) << "after read" << trackType;

    // maybe reset, return;
    if (track->packet_source == nullptr) {
      return;
    }

    size_t id = 0;
    size_t count = media_packets.size();

    for (; id < count; id++) {
      auto& media_packet = media_packets[id];
      track->packet_source->QueueAccessunit(media_packet);
      num_buffer++;
    }

    if (err == ave::WOULD_BLOCK) {
      break;
    }
    if (err != ave::OK) {
      // TODO(youfa)
      break;
    }
  }
}

status_t GenericSource::DoSeek(int64_t seek_time_us, SeekMode mode) {
  if (video_track_.source != nullptr) {
    int64_t actual_time_us = 0;
    ReadBuffer(MediaType::VIDEO, seek_time_us, mode, &actual_time_us);

    if (mode != SeekMode::SEEK_CLOSEST) {
      seek_time_us = std::max<int64_t>(0, actual_time_us);
    }
    video_last_dequeue_time_us_ = actual_time_us;
  }

  if (audio_track_.source != nullptr) {
    ReadBuffer(MediaType::AUDIO, seek_time_us, mode);
    audio_last_dequeue_time_us_ = seek_time_us;
  }

  if (subtitle_track_.source != nullptr) {
    subtitle_track_.packet_source->Clear();
  }

  if (timed_text_track_.source != nullptr) {
    timed_text_track_.packet_source->Clear();
  }

  return ave::OK;
}

void GenericSource::onMessageReceived(const std::shared_ptr<Message>& message) {
  std::lock_guard<std::mutex> lock(lock_);
  switch (message->what()) {
    case kWhatPrepare: {
      OnPrepare();
      break;
    }

    case kWhatReadBuffer: {
      OnReadBuffer(message);
      break;
    }

    case kWhatSeek: {
      int64_t seek_time_us = -1;
      int32_t mode = -1;
      AVE_CHECK(message->findInt64("seek_time_us", &seek_time_us));
      AVE_CHECK(message->findInt32("mode", &mode));

      std::shared_ptr<Message> response = std::make_shared<Message>();
      status_t err = DoSeek(seek_time_us, static_cast<SeekMode>(mode));
      response->setInt32("err", err);

      std::shared_ptr<ReplyToken> replyID;
      AVE_CHECK(message->senderAwaitsResponse(replyID));
      response->postReply(replyID);

      break;
    }
  }
}

void GenericSource::NotifyPrepared(status_t err) {
  auto notify = notify_.lock();
  if (notify != nullptr) {
    notify->OnPrepared(err);
  }
}

void GenericSource::NotifyFlagsChanged(int32_t flags) {
  auto notify = notify_.lock();
  if (notify != nullptr) {
    notify->OnFlagsChanged(flags);
  }
}

void GenericSource::NotifyVideoSizeChanged(
    std::shared_ptr<MediaFormat>& format) {
  auto notify = notify_.lock();
  if (notify != nullptr) {
    notify->OnVideoSizeChanged(format);
  }
}

void GenericSource::NotifyBuffering(int32_t percentage) {
  auto notify = notify_.lock();
  if (notify != nullptr) {
    notify->OnBufferingUpdate(percentage);
  }
}

}  // namespace avp
