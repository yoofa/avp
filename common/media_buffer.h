/*
 * media_buffer.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_MEDIA_BUFFER_H
#define AVP_MEDIA_BUFFER_H

#include "message.h"

namespace avp {
class MediaBuffer : MessageObject {
 public:
  MediaBuffer();
  virtual ~MediaBuffer();

 private:
};
}  // namespace avp

#endif /* !AVP_MEDIA_BUFFER_H */
