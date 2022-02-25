/*
 * Mutex.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_MUTEX_H
#define AVP_MUTEX_H

#include <pthread.h>

#include "thread_annotation.h"
#include "types.h"

namespace avp {

class Condition;

class CAPABILITY("mutex") Mutex {
 public:
  Mutex() { pthread_mutex_init(&mMutex, nullptr); }
  virtual ~Mutex() { pthread_mutex_destroy(&mMutex); }

  status_t lock() ACQUIRE() { return -pthread_mutex_lock(&mMutex); }
  status_t unlock() RELEASE() { return -pthread_mutex_unlock(&mMutex); }

  status_t tryLock() TRY_ACQUIRE(0) { return -pthread_mutex_trylock(&mMutex); }

  class SCOPED_CAPABILITY LockGuard {
   public:
    inline explicit LockGuard(Mutex& mutex) ACQUIRE(mutex) : mLock(mutex) {
      mLock.lock();
    }
    inline explicit LockGuard(Mutex* mutex) ACQUIRE(mutex) : mLock(*mutex) {
      mLock.lock();
    }
    inline ~LockGuard() RELEASE() { mLock.unlock(); }

   private:
    Mutex& mLock;
    LockGuard(const LockGuard&);
    LockGuard& operator=(const LockGuard&);
  };

 private:
  friend class Condition;

  Mutex(const Mutex&);
  Mutex& operator=(const Mutex&);

  pthread_mutex_t mMutex;
};

using lock_guard = Mutex::LockGuard;

}  // namespace avp

#endif /* !AVP_MUTEX_H */
