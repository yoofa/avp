/*
 * packet_source.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "packet_source.h"

#include "base/types.h"

namespace avp {

PacketSource::PacketSource(media_track_type track_type)
    : track_type_(track_type) {}

PacketSource::~PacketSource() = default;

status_t PacketSource::Start() {
  return ave::OK;
}
status_t PacketSource::Stop() {
  return ave::OK;
}

void PacketSource::Clear() {}

bool PacketSource::HasBufferAvailable(status_t* result) {
  std::unique_lock<std::mutex> l(lock_);
  *result = ave::OK;

  return !packets_.empty();
}

size_t PacketSource::GetAvailableBufferCount(status_t* result) {
  std::unique_lock<std::mutex> l(lock_);
  *result = ave::OK;

  if (!packets_.empty()) {
    return packets_.size();
  }

  return 0;
}

status_t PacketSource::QueueAccessunit(std::shared_ptr<MediaPacket> packet) {
  std::unique_lock<std::mutex> l(lock_);
  packets_.push(std::move(packet));
  return ave::OK;
}

status_t PacketSource::DequeueAccessUnit(std::shared_ptr<MediaPacket>& packet) {
  packet.reset();

  std::unique_lock<std::mutex> l(lock_);

  while (!packets_.size()) {
    condition_.wait(l);
  }

  if (packets_.size()) {
    packet = packets_.front();
    packets_.pop();
  }

  return ave::OK;
}

} /* namespace avp */
