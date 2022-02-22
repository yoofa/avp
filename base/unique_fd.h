/*
 * unique_fd.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef UNIQUE_FD_H
#define UNIQUE_FD_H

#include <unistd.h>

#include "base/constructor_magic.h"

namespace avp {

class unique_fd final {
 public:
  unique_fd() : value_(-1) {}

  explicit unique_fd(int value) : value_(value) {}

  ~unique_fd() { clear(); }

  unique_fd(unique_fd&& other) : value_(other.release()) {}

  unique_fd& operator=(unique_fd&& s) {
    reset(s.release());
    return *this;
  }

  void reset(int new_value) {
    if (value_ != -1) {
      // Even if close(2) fails with EINTR, the fd will have been closed.
      // Using TEMP_FAILURE_RETRY will either lead to EBADF or closing someone
      // else's fd.
      // http://lkml.indiana.edu/hypermail/linux/kernel/0509.1/0877.html
      close(value_);
    }
    value_ = new_value;
  }

  void clear() { reset(-1); }

  int get() const { return value_; }

  int release() __attribute__((warn_unused_result)) {
    int ret = value_;

    value_ = -1;

    return ret;
  }

 private:
  int value_;

  AVP_DISALLOW_COPY_AND_ASSIGN(unique_fd);
};

} /* namespace avp */

#endif /* !UNIQUE_FD_H */
