/*
 * buffer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <memory>

#include "base/constructor_magic.h"
#include "base/types.h"
#include "common/message.h"

namespace avp {

class Buffer {
 public:
  Buffer(size_t capacity);
  Buffer(void* data, size_t capacity);

  uint8_t* base() { return (uint8_t*)mData; }
  uint8_t* data() { return (uint8_t*)mData + mRangeOffset; }
  size_t capacity() const { return mCapacity; }
  size_t size() const { return mRangeLength; }
  size_t offset() const { return mRangeOffset; }
  void setRange(size_t offset, size_t size);

  // create buffer from dup of some memory block
  static std::shared_ptr<Buffer> CreateAsCopy(const void* data,
                                              size_t capacity);
  void setInt32Data(int32_t data) { mInt32Data = data; }
  int32_t int32Data() const { return mInt32Data; }
  std::shared_ptr<Message> meta();

  virtual ~Buffer();

 private:
  std::shared_ptr<Message> mMeta;
  void* mData;
  size_t mCapacity;
  size_t mRangeOffset;
  size_t mRangeLength;
  int32_t mInt32Data;
  bool mOwnsData;

  AVP_DISALLOW_COPY_AND_ASSIGN(Buffer);
};

} /* namespace avp */

#endif /* !BUFFER_H */
