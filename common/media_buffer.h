/*
 * media_buffer.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_MEDIA_BUFFER_H
#define AVP_MEDIA_BUFFER_H

#include "buffer.h"
#include "message.h"

namespace avp {
class MediaBuffer {
 public:
  MediaBuffer(std::shared_ptr<Message> meta, std::shared_ptr<Buffer> buffer);
  virtual ~MediaBuffer() = default;

  uint8_t* base();
  uint8_t* data();
  size_t capacity() const;
  size_t size() const;
  size_t offset() const;
  std::shared_ptr<Message> meta();

  void setRange(size_t offset, size_t size);
  void setMeta(std::shared_ptr<Message> meta);

 private:
  std::shared_ptr<Message> mMeta;
  std::shared_ptr<Buffer> mBuffer;
};
}  // namespace avp

#endif /* !AVP_MEDIA_BUFFER_H */
