/*
 * content_source.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef CONTENT_SOURCE_H
#define CONTENT_SOURCE_H

#include <memory>

#include "base/attributes.h"
#include "base/errors.h"

#include "media/foundation/handler.h"
#include "media/foundation/message.h"

#include "player_interface.h"

namespace avp {

using ave::Buffer;
using ave::Message;
using status_t = ave::status_t;

class ContentSource : public ave::Handler, public ave::MessageObject {
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

  ContentSource() = default;

  virtual void Prepare() = 0;
  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual void Pause() = 0;
  virtual void Resume() = 0;

  virtual status_t DequeueAccessUnit(bool audio,
                                     std::shared_ptr<Buffer>& accessUnit) = 0;
  virtual std::shared_ptr<Message> GetFormat(bool audio);
  virtual status_t GetDuration(int64_t* /* durationUs */) {
    return ave::INVALID_OPERATION;
  }

  virtual size_t getTrackCount() const { return 0; }
  virtual std::shared_ptr<Message> getTrackInfo(size_t /* trackIndex */) const {
    return nullptr;
  }
  virtual status_t SelectTrack(size_t /* trackIndex */,
                               bool /* select */) const {
    return ave::INVALID_OPERATION;
  }

  virtual status_t SeekTo(int64_t /* seekTimeUs */, SeekMode /* mode */) {
    return ave::INVALID_OPERATION;
  }

 protected:
  ~ContentSource() override = default;
  void onMessageReceived(const std::shared_ptr<Message>& message) override = 0;

  std::shared_ptr<Message> dupNotify() const { return mNotify->dup(); }

  void notifyFlagsChanged(uint32_t flags);
  void notifyVideoSizeChanged(const std::shared_ptr<Message> message = nullptr);
  void notifyPrepared(status_t err = ave::OK);

 private:
  friend class AvPlayer;
  void setNotifier(std::shared_ptr<Message> notify) {
    mNotify = std::move(notify);
  }
  std::shared_ptr<Message> mNotify;

  AVE_DISALLOW_COPY_AND_ASSIGN(ContentSource);
};

}  // namespace avp

#endif /* !CONTENT_SOURCE_H */
