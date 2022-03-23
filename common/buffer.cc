/*
 * buffer.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "buffer.h"

#include <cstring>
#include <memory>

#include "base/checks.h"

namespace avp {
Buffer::Buffer(size_t capacity)
    : mData(malloc(capacity)),
      mCapacity(capacity),
      mRangeOffset(0),
      mRangeLength(capacity),
      mInt32Data(0),
      mOwnsData(true) {}

Buffer::Buffer(void* data, size_t capacity)
    :

      mData(data),
      mCapacity(capacity),
      mRangeOffset(0),
      mRangeLength(capacity),
      mInt32Data(0),
      mOwnsData(false) {}

// static
std::shared_ptr<Buffer> Buffer::CreateAsCopy(const void* data,
                                             size_t capacity) {
  std::shared_ptr<Buffer> res(std::make_shared<Buffer>(capacity));
  memcpy(res->data(), data, capacity);
  return res;
}

Buffer::~Buffer() {
  if (mOwnsData) {
    if (mData != NULL) {
      free(mData);
      mData = NULL;
    }
  }
}

void Buffer::setRange(size_t offset, size_t size) {
  CHECK_LE(offset, mCapacity);
  CHECK_LE(offset + size, mCapacity);

  mRangeOffset = offset;
  mRangeLength = size;
}

std::shared_ptr<Message> Buffer::meta() {
  if (mMeta.get() == nullptr) {
    mMeta = std::make_shared<Message>();
  }
  return mMeta;
}

} /* namespace avp */
