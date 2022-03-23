/*
 * media_buffer.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "media_buffer.h"

#include "common/buffer.h"

namespace avp {

MediaBuffer::MediaBuffer(std::shared_ptr<Message> meta,
                         std::shared_ptr<Buffer> buffer)
    : mMeta(std::move(meta)), mBuffer(std::move(buffer)) {}

uint8_t* MediaBuffer::base() {
  return mBuffer->base();
}

uint8_t* MediaBuffer::data() {
  return mBuffer->data();
}
size_t MediaBuffer::capacity() const {
  return mBuffer->capacity();
}

size_t MediaBuffer::size() const {
  return mBuffer->size();
}

size_t MediaBuffer::offset() const {
  return mBuffer->offset();
}

std::shared_ptr<Message> MediaBuffer::meta() {
  return mMeta;
}

void MediaBuffer::setRange(size_t offset, size_t size) {
  mBuffer->setRange(offset, size);
}

void MediaBuffer::setMeta(std::shared_ptr<Message> meta) {
  mMeta->clear();
  mMeta = std::move(meta);
}

}  // namespace avp
