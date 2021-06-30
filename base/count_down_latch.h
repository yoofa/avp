/*
 * count_down_latch.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_COUNT_DOWN_LATCH_H
#define AVP_COUNT_DOWN_LATCH_H

#include <condition_variable>
#include <mutex>

#include "noncopyable.h"

namespace avp {

class CountDownLatch : noncopyable {
public:
  explicit CountDownLatch(int count);

  void wait();
  void countDown();
  int getCount() const;

private:
  mutable std::mutex mMutex;
  std::condition_variable mCondition;
  int mCount;
};

} // namespace avp

#endif /* !AVP_COUNT_DOWN_LATCH_H */
