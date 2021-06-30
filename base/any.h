/*
 * avp.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_ANY_H
#define AVP_ANY_H

#include <memory>
#include <utility>

class Any {
 public:
  Any() = default;
  virtual ~Any() = default;

  template <typename T, typename... Args>
  void set(Args&&... args) {
    mData.reset(new T(std::forward<Args>(args)...),
                [](void* ptr) { delete (T*)ptr; });
  }

  template <typename T>
  T& get() {
    if (!mData) {
      return nullptr;
    }
    T* ptr = (T*)mData.get();
    return *ptr;
  }

  operator bool() { return mData.operator bool(); }

  bool empty() { return !bool(); }

 private:
  std::shared_ptr<void> mData;
};

#endif /* !AVP_ANY_H */
