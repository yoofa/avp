/*
 * Handler.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_HANDLER_H
#define AVP_HANDLER_H

#include <memory>

#include "base/constructor_magic.h"
#include "looper.h"

namespace avp {

class Message;

class Handler : public std::enable_shared_from_this<Handler> {
 public:
  Handler() : mId(0), mMessageCounter(0) {}
  virtual ~Handler() = default;

  Looper::handler_id id() const { return mId; }

  std::shared_ptr<Looper> looper() const { return mLooper.lock(); }

  std::weak_ptr<Looper> getLooper() const { return mLooper; }

 protected:
  virtual void onMessageReceived(const std::shared_ptr<Message>& message) = 0;

 private:
  friend class Message;
  friend class HandlerRoster;

  Looper::handler_id mId;
  std::weak_ptr<Looper> mLooper;

  uint32_t mMessageCounter;

  inline void setId(Looper::handler_id id,
                    const std::weak_ptr<Looper>& looper) {
    mId = id;
    mLooper = looper;
  }

  void deliverMessage(const std::shared_ptr<Message>& message);

  AVP_DISALLOW_COPY_AND_ASSIGN(Handler);
};

}  // namespace avp

#endif /* !AVP_HANDLER_H */
