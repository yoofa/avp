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
#include "base/errors.h"
#include "common/buffer.h"
#include "common/handler.h"
#include "common/message.h"
#include "common/meta_data.h"
#include "player/audio_sink.h"
#include "player/video_sink.h"

namespace avp {

class PlayerBase {
 public:
  enum media_track_type {
    MEDIA_TRACK_TYPE_UNKNOWN = 0,
    MEDIA_TRACK_TYPE_VIDEO = 1,
    MEDIA_TRACK_TYPE_AUDIO = 2,
    MEDIA_TRACK_TYPE_TIMEDTEXT = 3,
    MEDIA_TRACK_TYPE_SUBTITLE = 4,
    MEDIA_TRACK_TYPE_METADATA = 5,
  };

  enum SeekMode : int32_t {
    SEEK_PREVIOUS_SYNC = 0,
    SEEK_NEXT_SYNC = 1,
    SEEK_CLOSEST_SYNC = 2,
    SEEK_CLOSEST = 4,
    SEEK_FRAME_INDEX = 8,
    SEEK = 8,
    NONBLOCKING = 16,
  };

  class Listener {
   protected:
    Listener() = default;
    virtual ~Listener() = default;

   public:
    virtual void notify(int what, std::shared_ptr<Message> info) = 0;
  };

  class ContentSource : public Handler, public MessageObject {
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
    ContentSource() {}

    virtual void prepare() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;

    virtual std::shared_ptr<MetaData> getSourceMeta() = 0;
    virtual std::shared_ptr<MetaData> getMeta(bool audio) = 0;

    virtual status_t dequeueAccessUnit(bool audio,
                                       std::shared_ptr<Buffer>& accessUnit) = 0;
    virtual std::shared_ptr<Message> getFormat(bool audio);
    virtual status_t getDuration(int64_t* /* durationUs */) {
      return INVALID_OPERATION;
    }

    virtual size_t getTrackCount() const { return 0; }
    virtual std::shared_ptr<Message> getTrackInfo(size_t trackIndex) const {
      return nullptr;
    }
    virtual status_t selectTrack(size_t trackIndex, bool select) const {
      return INVALID_OPERATION;
    }

    virtual status_t seekTo(
        int64_t /* seekTimeUs */,
        SeekMode /* mode */ = SeekMode::SEEK_PREVIOUS_SYNC) {
      return INVALID_OPERATION;
    }

   protected:
    virtual ~ContentSource() {}
    virtual void onMessageReceived(const std::shared_ptr<Message>& message) = 0;

    std::shared_ptr<Message> dupNotify() const { return mNotify->dup(); }

    void notifyFlagsChanged(uint32_t flags);
    void notifyVideoSizeChanged(
        const std::shared_ptr<Message> message = nullptr);
    void notifyPrepared(status_t err = OK);

   private:
    friend class AvPlayer;
    void setNotifier(std::shared_ptr<Message> notify) {
      mNotify = std::move(notify);
    }
    std::shared_ptr<Message> mNotify;

    AVP_DISALLOW_COPY_AND_ASSIGN(ContentSource);
  };
  class AudioDecoder {
   protected:
    AudioDecoder() = default;
    virtual ~AudioDecoder() = default;

   private:
    /* data */
  };

  class VideoDecoder {
   protected:
    VideoDecoder() = default;
    virtual ~VideoDecoder() = default;

   private:
    /* data */
  };

  // Notify Event
  enum {
    kWhatSetDataSourceCompleted,
    kWhatPrepared,
  };

  PlayerBase() = default;
  ~PlayerBase() = default;

  virtual status_t setListener(const std::shared_ptr<Listener>& listener) = 0;
  virtual status_t init() = 0;

  virtual status_t setDataSource(const char* url) = 0;
  virtual status_t setDataSource(int fd, int64_t offset, int64_t length) = 0;
  virtual status_t setDataSource(
      const std::shared_ptr<ContentSource>& source) = 0;

  virtual status_t setAudioSink(std::shared_ptr<AudioSink> sink) = 0;
  virtual status_t setVideoSink(std::shared_ptr<VideoSink> sink) = 0;

  virtual status_t prepare() = 0;

  virtual status_t start() = 0;
  virtual status_t stop() = 0;

  virtual status_t pause() = 0;
  virtual status_t resume() = 0;
  virtual status_t seekTo(int msec,
                          SeekMode mode = SeekMode::SEEK_PREVIOUS_SYNC) = 0;

  virtual status_t reset() = 0;

  void notifyListner(int what, std::shared_ptr<Message> msg) {
    std::shared_ptr<Listener> listener = mListener.lock();

    if (listener != nullptr) {
      listener->notify(what, msg);
    }
  }

 protected:
  std::weak_ptr<Listener> mListener;
};

}  // namespace avp

#endif /* !AVP_PLAYER_INTERFACE_H */
