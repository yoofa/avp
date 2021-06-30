/*
 * Condition.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_CONDITION_H
#define AVP_CONDITION_H

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#include "mutex.h"
#include "types.h"

namespace avp {
class Condition {
 public:
  Condition() { pthread_cond_init(&mCond, nullptr); }
  virtual ~Condition() { pthread_cond_destroy(&mCond); }

  status_t wait(Mutex& mutex) {
    return -pthread_cond_wait(&mCond, &mutex.mMutex);
  }
  status_t waitRelative(Mutex& mutex, nsecs_t reltime) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t reltime_sec = reltime / 1000000000;

    ts.tv_nsec += static_cast<long>(reltime % 1000000000);
    if (reltime_sec < INT64_MAX && ts.tv_nsec >= 1000000000) {
      ts.tv_nsec -= 1000000000;
      ++reltime_sec;
    }

    int64_t time_sec = ts.tv_sec;
    if (time_sec > INT64_MAX - reltime_sec) {
      time_sec = INT64_MAX;
    } else {
      time_sec += reltime_sec;
    }

    ts.tv_sec = (time_sec > LONG_MAX) ? LONG_MAX : static_cast<long>(time_sec);

    return -pthread_cond_timedwait(&mCond, &mutex.mMutex, &ts);
  }

  void signal() { pthread_cond_signal(&mCond); }

  void broadcast() { pthread_cond_broadcast(&mCond); }

 private:
  pthread_cond_t mCond;
};

}  // namespace avp

#endif /* !AVP_CONDITION_H */
