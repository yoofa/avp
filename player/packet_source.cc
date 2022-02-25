/*
 * packet_source.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "packet_source.h"

#include "base/types.h"

namespace avp {
PacketSource::PacketSource() {}

PacketSource::~PacketSource() {}

status_t PacketSource::start() {
  return OK;
}
status_t PacketSource::stop() {
  return OK;
}

void PacketSource::clear() {}

bool PacketSource::hasBufferAvailable(status_t* result) {
  std::unique_lock<std::mutex> l(mLock);
  *result = OK;

  if (!mBuffers.empty()) {
    return true;
  }

  return false;
}

size_t PacketSource::getAvailableBufferCount(status_t* result) {
  std::unique_lock<std::mutex> l(mLock);
  *result = OK;

  if (!mBuffers.empty()) {
    return mBuffers.size();
  }

  return 0;
}

status_t PacketSource::queueAccessunit(std::shared_ptr<Buffer> buffer) {
  std::unique_lock<std::mutex> l(mLock);
  mBuffers.push_back(buffer);
  return OK;
}

status_t PacketSource::dequeueAccessUnit(std::shared_ptr<Buffer>& buffer) {
  buffer.reset();

  std::unique_lock<std::mutex> l(mLock);

  while (!mBuffers.size()) {
    mCondition.wait(l);
  }

  if (mBuffers.size()) {
    buffer = *mBuffers.begin();
    mBuffers.pop_front();
  }

  return OK;
}

} /* namespace avp */
