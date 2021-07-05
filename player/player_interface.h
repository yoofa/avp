/*
 * avplayer.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_PLAYER_INTERFACE_H
#define AVP_PLAYER_INTERFACE_H

#include <memory>

#include "base/constructor_magic.h"
#include "base/error.h"
#include "common/handler.h"
#include "common/message.h"

namespace avp {

class PlayerBase {
 public:
  class ContentSource : public Handler {
   public:
    enum class Flags {
      FLAG_CAN_PAUSE = 1,
      FLAG_CAN_SEEK_BACKWARD = 2,  // the "10 sec back button"
      FLAG_CAN_SEEK_FORWARD = 4,   // the "10 sec forward button"
      FLAG_CAN_SEEK = 8,           // the "seek bar"
      FLAG_DYNAMIC_DURATION = 16,
      FLAG_SECURE = 32,  // Secure codec is required.
      FLAG_PROTECTED =
          64,  // The screen needs to be protected (screenshot is disabled).
    };

    // Message type
    enum {
      kWhatPrepared,
      kWhatFlagsChanged,
      kWhatVideoSizeChanged,
      kWhatBufferingUpdate,
      kWhatPauseOnBufferingStart,
      kWhatResumeOnBufferingEnd,
      kWhatCacheStats,
      kWhatSubtitleData,
      kWhatTimedTextData,
      kWhatTimedMetaData,
      kWhatQueueDecoderShutdown,
      kWhatDrmNoLicense,
      kWhatInstantiateSecureDecoders,
      // Modular DRM
      kWhatDrmInfo,
    };

    explicit ContentSource(std::shared_ptr<Message> notify) : mNotify(notify) {}
    virtual ~ContentSource() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;

    virtual status_t dequeueAccussUnit(bool audio) = 0;

    virtual size_t getTrackCount() const { return 0; }
    virtual std::shared_ptr<Message> getTrackInfo(size_t trackIndex) const {
      return nullptr;
    }
    virtual status_t selectTrack(size_t trackIndex, bool select) const {
      return 0;
    }

   protected:
    virtual void onMessageReceived(
        const std::shared_ptr<Message>& message) override;
    std::shared_ptr<Message> dupNotify() const { return mNotify->dup(); }

   private:
    std::shared_ptr<Message> mNotify;

    AVP_DISALLOW_COPY_AND_ASSIGN(ContentSource);
  };

  class AudioSink {
   public:
    AudioSink() = default;
    virtual ~AudioSink() = default;

   private:
    /* data */
  };

  class VideoSink {
   public:
    VideoSink() = default;
    virtual ~VideoSink() = default;

   private:
    /* data */
  };

  enum class SeekMode {
    SEEK_PREVIOUS_SYNC = 0,
    SEEK_NEXT_SYNC,
    SEEK_CLOSET,
  };

  PlayerBase() = default;
  ~PlayerBase() = default;

  virtual status_t init() = 0;

  virtual status_t setDataSource(int fd, int64_t offset, int64_t length) = 0;

  virtual status_t prepare() = 0;

  virtual status_t start() = 0;
  virtual status_t stop() = 0;

  virtual status_t pause() = 0;
  virtual status_t resume() = 0;
  virtual status_t seekTo(int msec,
                          SeekMode mode = SeekMode::SEEK_PREVIOUS_SYNC) = 0;

  virtual status_t reset() = 0;

 private:
  /* data */
};

}  // namespace avp

#endif /* !AVP_PLAYER_INTERFACE_H */
