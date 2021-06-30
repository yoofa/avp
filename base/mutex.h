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

  class SCOPED_CAPABILITY Autolock {
   public:
    inline explicit Autolock(Mutex& mutex) ACQUIRE(mutex) : mLock(mutex) {
      mLock.lock();
    }
    inline explicit Autolock(Mutex* mutex) ACQUIRE(mutex) : mLock(*mutex) {
      mLock.lock();
    }
    inline ~Autolock() RELEASE() { mLock.unlock(); }

   private:
    Mutex& mLock;
    Autolock(const Autolock&);
    Autolock& operator=(const Autolock&);
  };

 private:
  friend class Condition;

  Mutex(const Mutex&);
  Mutex& operator=(const Mutex&);

  pthread_mutex_t mMutex;
};

typedef Mutex::Autolock AutoMutex;

}  // namespace avp

#endif /* !AVP_MUTEX_H */
