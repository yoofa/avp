/*
 * noncopyable.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_NONCOPYABLE_H
#define AVP_NONCOPYABLE_H

namespace avp {
class noncopyable {
public:
  noncopyable(const noncopyable &) = delete;
  void operator=(const noncopyable) = delete;

protected:
  noncopyable() = default;
  ~noncopyable() = default;
};

} // namespace avp

#endif /* !AVP_NONCOPYABLE_H */
