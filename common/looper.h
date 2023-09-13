/*
 * Looper.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_LOOPER_H
#define AVP_LOOPER_H

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "base/constructor_magic.h"
#include "base/count_down_latch.h"
#include "base/errors.h"

namespace avp {

class Message;
class Handler;
class ReplyToken;

class Looper : public std::enable_shared_from_this<Looper> {
 public:
  typedef int32_t event_id;
  typedef int32_t handler_id;
  Looper();
  virtual ~Looper();

  // set looper name
  void setName(std::string name);
  handler_id registerHandler(const std::shared_ptr<Handler> handler);
  void unregisterHandler(handler_id handlerId);

  int32_t start(int32_t priority = 0);
  int32_t stop();
  void post(const std::shared_ptr<Message>& message, const int64_t delayUs);

  static int64_t getNowUs() {
    auto systemClock = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
               systemClock.time_since_epoch())
        .count();
  }

 private:
  friend class Message;

  struct Event {
    int64_t mWhenUs;
    std::shared_ptr<Message> mMessage;
  };

  struct EventOrder {
    bool operator()(const std::unique_ptr<Event>& first,
                    const std::unique_ptr<Event>& second) const {
      return first->mWhenUs > second->mWhenUs;
    }
  };

  std::string mName;
  int32_t mPriority;
  std::unique_ptr<std::thread> mThread;
  bool mLooping;
  CountDownLatch mStartLatch;
  bool mStoped;
  std::mutex mMutex;
  std::condition_variable mCondition;
  std::priority_queue<std::unique_ptr<Event>,
                      std::vector<std::unique_ptr<Event>>,
                      EventOrder>
      mEventQueue;

  std::condition_variable mRepliesCondition;

  void loop();
  bool keepRunning();

  std::shared_ptr<ReplyToken> createReplyToken();

  status_t awaitResponse(const std::shared_ptr<ReplyToken>& replyToken,
                         std::shared_ptr<Message>& response);

  status_t postReply(const std::shared_ptr<ReplyToken>& replyToken,
                     const std::shared_ptr<Message>& reply);

  AVP_DISALLOW_COPY_AND_ASSIGN(Looper);
};

}  // namespace avp

#endif /* !AVP_LOOPER_H */
