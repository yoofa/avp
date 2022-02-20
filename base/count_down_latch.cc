/*
 * count_down_latch.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "count_down_latch.h"

namespace avp {

CountDownLatch::CountDownLatch(int count) : mCount(count) {}

void CountDownLatch::wait() {
  std::unique_lock<std::mutex> l(mMutex);
  while (mCount > 0) {
    mCondition.wait(l);
  }
}

void CountDownLatch::countDown() {
  std::unique_lock<std::mutex> l(mMutex);
  --mCount;
  if (mCount == 0) {
    mCondition.notify_all();
  }
}

int CountDownLatch::getCount() const {
  std::lock_guard<std::mutex> l(mMutex);
  return mCount;
}

}  // namespace avp
