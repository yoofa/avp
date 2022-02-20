/*
 * Looper.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "looper.h"

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../base/count_down_latch.h"
#include "handler_roster.h"
#include "message.h"

namespace avp {

HandlerRoster gRoster;

Looper::Looper()
    : mThread(nullptr), mLooping(false), mStartLatch(1), mStoped(false) {}

Looper::~Looper() {
  stop();
}

void Looper::setName(std::string name) {
  mName = name;
}

Looper::handler_id Looper::registerHandler(
    const std::shared_ptr<Handler> handler) {
  return gRoster.registerHandler(shared_from_this(), handler);
}

void Looper::unregisterHandler(handler_id handlerId) {
  gRoster.unregisterHandler(handlerId);
}

int32_t Looper::start(int32_t priority) {
  std::lock_guard<std::mutex> guard(mMutex);
  if (mThread.get()) {
    return -1;
  }

  mThread = std::make_unique<std::thread>(&Looper::loop, this);
  mLooping = true;
  mStartLatch.wait();
  return 0;
}

int32_t Looper::stop() {
  // TODO(youfa) support stop in loop thread.
  {
    std::lock_guard<std::mutex> guard(mMutex);
    mStoped = true;
    mLooping = false;
    mCondition.notify_all();
  }
  if (mThread.get()) {
    mThread->join();
    mThread.release();
  }
  return 0;
}

void Looper::post(const std::shared_ptr<Message>& message,
                  const int64_t delayUs) {
  std::lock_guard<std::mutex> guard(mMutex);
  if (mStoped) {
    return;
  }
  int64_t whenUs;
  if (delayUs > 0) {
    int64_t nowUs = getNowUs();
    whenUs = (delayUs > (std::numeric_limits<int64_t>::max() - nowUs)
                  ? std::numeric_limits<int64_t>::max()
                  : (nowUs + delayUs));
  } else {
    whenUs = getNowUs();
  }

  std::unique_ptr<Event> event = std::make_unique<Event>();
  event->mWhenUs = whenUs;
  event->mMessage = std::move(message);
  mEventQueue.push(std::move(event));
  mCondition.notify_all();
}

void Looper::loop() {
  mStartLatch.countDown();
  while (keepRunning()) {
    std::shared_ptr<Message> message;
    {
      std::unique_lock<std::mutex> l(mMutex);

      if (mEventQueue.size() == 0) {
        mCondition.wait(l);
        continue;
      }

      auto& event = mEventQueue.top();
      int64_t nowUs = getNowUs();

      if (event->mWhenUs > nowUs) {
        int64_t delayUs = event->mWhenUs - nowUs;
        if (delayUs > std::numeric_limits<int64_t>::max()) {
          delayUs = std::numeric_limits<int64_t>::max();
        }

        mCondition.wait_for(l, std::chrono::microseconds(delayUs));
        continue;
      }

      message = std::move(event->mMessage);
      mEventQueue.pop();
    }
    message->deliver();
    message.reset();
  }
}

bool Looper::keepRunning() {
  std::lock_guard<std::mutex> l(mMutex);
  return mLooping || (mEventQueue.size() > 0);
}

std::shared_ptr<ReplyToken> Looper::createReplyToken() {
  return std::make_shared<ReplyToken>(shared_from_this());
}

status_t Looper::awaitResponse(const std::shared_ptr<ReplyToken>& replyToken,
                               std::shared_ptr<Message>& response) {
  std::unique_lock<std::mutex> guard(mMutex);
  // CHECK(replyToken != NULL)
  while (!replyToken->getReply(response)) {
    mRepliesCondition.wait(guard);
  }

  return 0;
}

status_t Looper::postReply(const std::shared_ptr<ReplyToken>& replyToken,
                           const std::shared_ptr<Message>& reply) {
  std::lock_guard<std::mutex> guard(mMutex);
  status_t err = replyToken->setReply(reply);
  if (err == 0) {
    mRepliesCondition.notify_all();
  }
  return err;
}

}  // namespace avp
