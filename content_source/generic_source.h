/*
 * generic_source.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_GENERIC_SOURCE_H
#define AVP_GENERIC_SOURCE_H

#include <memory>
#include <vector>

#include "base/data_source/data_source.h"
#include "base/thread_annotation.h"
#include "base/unique_fd.h"
#include "media/foundation/handler.h"
#include "media/foundation/looper.h"
#include "media/foundation/media_packet.h"
#include "media/foundation/media_source.h"
#include "media/foundation/message.h"

#include "api/content_source/content_source.h"
#include "api/demuxer/demuxer.h"
#include "api/demuxer/demuxer_factory.h"
#include "player/packet_source.h"

namespace ave {
namespace player {

using ave::media::Buffer;
using ave::media::Handler;
using ave::media::MediaPacket;
using ave::media::MediaSource;
using ave::media::MediaType;
using ave::media::Message;

class GenericSource : public Handler, public ContentSource {
 public:
  explicit GenericSource(std::shared_ptr<DemuxerFactory> demuxer_factory);
  ~GenericSource() override;

  void SetNotify(Notify* notify) override;

  status_t SetDataSource(const char* url /*, http_downloader*/);
  status_t SetDataSource(int fd, int64_t offset, int64_t length);
  status_t SetDataSource(std::shared_ptr<ave::DataSource> data_source);

  void Prepare() override;
  void Start() override;
  void Stop() override;
  void Pause() override;
  void Resume() override;
  status_t SeekTo(int64_t seek_time_us, SeekMode mode) override;

  std::shared_ptr<MediaFormat> GetFormat() override;

  status_t DequeueAccessUnit(
      MediaType track_type,
      std::shared_ptr<MediaPacket>& access_unit) override;

  status_t GetDuration(int64_t* duration_us) override;

  size_t GetTrackCount() const override;

  std::shared_ptr<MediaFormat> GetTrackInfo(size_t track_index) const override;
  std::shared_ptr<MediaFormat> GetTrackInfo(
      MediaType track_type) const override;

  status_t SelectTrack(size_t track_index, bool select) override;

 protected:
  void onMessageReceived(const std::shared_ptr<Message>& message) override;

 private:
  enum {
    kWhatPrepare,
    kWhatFetchSubtitleData,
    kWhatFetchTimedTextData,
    kWhatSendSubtitleData,
    kWhatSendGlobalTimedTextData,
    kWhatSendTimedTextData,
    kWhatChangeAVSource,
    kWhatPollBuffering,
    kWhatSeek,
    kWhatReadBuffer,
    kWhatStart,
    kWhatStop,
    kWhatPause,
    kWhatResume,
    kWhatSecureDecodersInstantiated,
  };

  status_t InitFromDataSource() REQUIRES(lock_);
  status_t StartSources() REQUIRES(lock_);
  void NotifyPreparedAndCleanup(status_t err) REQUIRES(lock_);
  void FinishPrepare() REQUIRES(lock_);
  void ResetDataSource() REQUIRES(lock_);
  void OnPrepare() REQUIRES(lock_);
  void PostReadBuffer(MediaType track_type) REQUIRES(lock_);
  void OnReadBuffer(const std::shared_ptr<Message>& message) REQUIRES(lock_);
  void ReadBuffer(MediaType track_type,
                  int64_t seek_time_us = -1ll,
                  SeekMode seek_mode = SeekMode::SEEK_PREVIOUS_SYNC,
                  int64_t* actual_time_us = nullptr) REQUIRES(lock_);
  status_t DoSeek(int64_t seek_time_us, SeekMode mode) REQUIRES(lock_);

  void SchedulePollBuffering() REQUIRES(lock_);
  void OnPollBuffering() REQUIRES(lock_);

  void NotifyPrepared(status_t err = ave::OK) REQUIRES(lock_);
  void NotifyFlagsChanged(int32_t flags) REQUIRES(lock_);
  void NotifyVideoSizeChanged(std::shared_ptr<MediaFormat>& format)
      REQUIRES(lock_);
  void NotifyBuffering(int32_t percentage) REQUIRES(lock_);

  struct Track {
    size_t index;
    MediaType media_type;
    std::shared_ptr<MediaSource> source;
    std::shared_ptr<PacketSource> packet_source;
  };

  Notify* notify_ GUARDED_BY(lock_);
  std::string uri_ GUARDED_BY(lock_);
  ave::base::unique_fd fd_ GUARDED_BY(lock_);
  int64_t offset_ GUARDED_BY(lock_);
  int64_t length_ GUARDED_BY(lock_);
  std::shared_ptr<ave::DataSource> data_source_ GUARDED_BY(lock_);
  std::shared_ptr<MediaFormat> source_format_ GUARDED_BY(lock_);
  int64_t duration_us_ GUARDED_BY(lock_);
  int64_t bitrate_ GUARDED_BY(lock_);

  std::shared_ptr<DemuxerFactory> demuxer_factory_;
  std::shared_ptr<Demuxer> demuxer_;

  std::vector<std::shared_ptr<MediaSource>> sources_ GUARDED_BY(lock_);
  // selected track indices
  Track audio_track_ GUARDED_BY(lock_);
  Track video_track_ GUARDED_BY(lock_);
  Track subtitle_track_ GUARDED_BY(lock_);
  Track timed_text_track_ GUARDED_BY(lock_);

  int64_t audio_last_dequeue_time_us_ GUARDED_BY(lock_);
  int64_t video_last_dequeue_time_us_ GUARDED_BY(lock_);
  uint64_t pending_read_buffer_types_ GUARDED_BY(lock_);

  bool preparing_;
  bool started_;
  bool is_streaming_;

  mutable std::mutex lock_;
  std::shared_ptr<ave::media::Looper> looper_;
};

}  // namespace player
}  // namespace ave

#endif /* !AVP_GENERIC_SOURCE_H */
