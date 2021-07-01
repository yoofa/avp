/*
 * HandlerRoster.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_HANDLERROSTER_H
#define AVP_HANDLERROSTER_H

#include <memory>
#include <mutex>
#include <unordered_map>

#include "base/constructor_magic.h"
#include "looper.h"

namespace avp {

class HandlerRoster {
 public:
  HandlerRoster();
  virtual ~HandlerRoster() = default;

  Looper::handler_id registerHandler(const std::shared_ptr<Looper> looper,
                                     const std::shared_ptr<Handler> handler);

  void unregisterHandler(Looper::handler_id handlerId);

 private:
  struct HandlerInfo {
    std::weak_ptr<Looper> mLooper;
    std::weak_ptr<Handler> mHandler;
  };

  std::mutex mMutex;
  std::unordered_map<Looper::handler_id, HandlerInfo> mHandlers;

  Looper::handler_id mNextHandlerId;

  AVP_DISALLOW_COPY_AND_ASSIGN(HandlerRoster);
};

}  // namespace avp

#endif /* !AVP_HANDLERROSTER_H */
