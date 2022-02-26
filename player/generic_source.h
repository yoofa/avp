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

#include "base/errors.h"
#include "base/unique_fd.h"
#include "common/looper.h"
#include "common/message.h"
#include "common/meta_data.h"
#include "player/data_source.h"
#include "player/demuxer_factory.h"
#include "player/media_source.h"
#include "player/packet_source.h"
#include "player_interface.h"

namespace avp {

using media_track_type = PlayerBase::media_track_type;
using SeekMode = PlayerBase::SeekMode;

class GenericSource : public PlayerBase::ContentSource {
 public:
  GenericSource();
  virtual ~GenericSource();

  status_t setDataSource(const char* url);
  status_t setDataSource(int fd, int64_t offset, int64_t length);

  void prepare() override;
  void start() override;
  void stop() override;
  void pause() override;
  void resume() override;
  virtual status_t seekTo(
      int64_t seekTimeUs,
      SeekMode mode = SeekMode ::SEEK_PREVIOUS_SYNC) override;

  std::shared_ptr<MetaData> getSourceMeta() override;
  std::shared_ptr<MetaData> getMeta(bool audio) override;

  status_t dequeueAccussUnit(bool audio,
                             std::shared_ptr<Buffer>& accessUnit) override;
  status_t getDuration(int64_t* durationUs) override;
  size_t getTrackCount() const override;
  std::shared_ptr<Message> getTrackInfo(size_t trackIndex) const override;
  status_t selectTrack(size_t trackIndex, bool select) const override;

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
    kWhatResume,
    kWhatSecureDecodersInstantiated,
  };

  status_t initFromDataSource();
  status_t startSources();
  void notifyPreparedAndCleanup(status_t err);
  void finishPrepare();
  void resetDataSource();
  void onPrepare();
  void postReadBuffer(media_track_type trackType);
  void onReadBuffer(const std::shared_ptr<Message>& msg);
  void readBuffer(media_track_type trackType,
                  int64_t seekTimeUs = -1ll,
                  SeekMode seekMode = SeekMode::SEEK_PREVIOUS_SYNC,
                  int64_t* actualTimeUs = nullptr);
  status_t doSeek(int64_t seekTimeUs, SeekMode mode);

  struct Track {
    size_t mIndex;
    std::shared_ptr<MediaSource> mSource;
    std::shared_ptr<PacketSource> mPacketSource;
  };

  std::vector<std::shared_ptr<MediaSource>> mSources;
  Track mAudioTrack;
  // int64_t mAudioTimeUs;
  int64_t mAudioLastDequeueTimeUs;
  Track mVideoTrack;
  // int64_t mVideoTimeUs;
  int64_t mVideoLastDequeueTimeUs;
  Track mSubtitleTrack;
  Track mTimedTextTrack;
  uint32_t mPendingReadBufferTypes;

  std::string mUri;
  unique_fd mFd;
  int64_t mOffset;
  int64_t mLength;
  std::shared_ptr<DataSource> mDataSource;
  std::unique_ptr<DemuxerFactory> mDemuxerFactory;
  std::shared_ptr<MetaData> mSourceMeta;
  std::shared_ptr<Demuxer> mDemuxer;

  int64_t mDurationUs;
  int32_t mBitrate;

  mutable std::mutex mLock;
  std::shared_ptr<Looper> mLooper;
};

}  // namespace avp

#endif /* !AVP_GENERIC_SOURCE_H */
