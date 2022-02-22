/*
 * generic_source.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_GENERIC_SOURCE_H
#define AVP_GENERIC_SOURCE_H

#include <memory>

#include "base/errors.h"
#include "base/unique_fd.h"
#include "common/looper.h"
#include "common/message.h"
#include "player/data_source.h"
#include "player/demuxer_factory.h"
#include "player/media_source.h"
#include "player_interface.h"

namespace avp {

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

  status_t initFromDataSource();
  status_t dequeueAccussUnit(bool audio) override;
  size_t getTrackCount() const override;
  std::shared_ptr<Message> getTrackInfo(size_t trackIndex) const override;
  status_t selectTrack(size_t trackIndex, bool select) const override;

  void onPrepare();

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

  struct Track {
    size_t mIndex;
    std::shared_ptr<MediaSource> mSource;
  };

  void resetDataSource();

  std::string mUri;
  unique_fd mFd;
  int64_t mOffset;
  int64_t mLength;
  std::shared_ptr<DataSource> mDataSource;
  std::unique_ptr<DemuxerFactory> mDemuxerFactory;

  mutable std::mutex mLock;
  std::shared_ptr<Looper> mLooper;
};

}  // namespace avp

#endif /* !AVP_GENERIC_SOURCE_H */
