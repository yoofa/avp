/*
 * packet_source.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "packet_source.h"

#include "base/logging.h"

namespace ave {
namespace player {

PacketSource::PacketSource(std::shared_ptr<MediaMeta> format)
    : format_(std::move(format)) {}

PacketSource::~PacketSource() = default;

status_t PacketSource::Start() {
  return ave::OK;
}

status_t PacketSource::Stop() {
  return ave::OK;
}

void PacketSource::Clear() {
  std::scoped_lock l(lock_);
  std::queue<std::shared_ptr<MediaFrame>> empty;
  packets_.swap(empty);
  eos_result_ = ave::OK;
  condition_.notify_all();
}

void PacketSource::SetFormat(std::shared_ptr<MediaMeta> format) {
  std::scoped_lock l(lock_);
  format_ = std::move(format);
}

bool PacketSource::HasBufferAvailable(status_t* result) {
  std::scoped_lock l(lock_);
  *result = ave::OK;

  if (!packets_.empty()) {
    return true;
  }

  *result = eos_result_;
  return false;
}

size_t PacketSource::GetAvailableBufferCount(status_t* result) {
  std::scoped_lock l(lock_);
  *result = ave::OK;

  if (!packets_.empty()) {
    return packets_.size();
  }

  *result = eos_result_;
  return 0;
}

status_t PacketSource::QueueAccessunit(std::shared_ptr<MediaFrame> packet) {
  std::scoped_lock l(lock_);
  packets_.push(std::move(packet));
  AVE_LOG(LS_VERBOSE) << "PacketSource::QueueAccessunit: source=" << this
                      << " size=" << packets_.size();
  condition_.notify_one();
  return ave::OK;
}

status_t PacketSource::DequeueAccessUnit(std::shared_ptr<MediaFrame>& packet)
    NO_THREAD_SAFETY_ANALYSIS {
  packet.reset();
  std::unique_lock<std::mutex> l(lock_);

  if (packets_.empty() && eos_result_ == ave::OK) {
    AVE_LOG(LS_VERBOSE) << "PacketSource::DequeueAccessUnit: waiting...";
  }

  while (packets_.empty() && eos_result_ == ave::OK) {
    condition_.wait(l);
  }

  if (!packets_.empty()) {
    packet = packets_.front();
    packets_.pop();
    AVE_LOG(LS_VERBOSE)
        << "PacketSource::DequeueAccessUnit: dequeued, size left="
        << packets_.size();
    return ave::OK;
  }

  return eos_result_;
}

void PacketSource::SignalEOS(status_t result) {
  if (result == ave::OK) {
    AVE_LOG(LS_ERROR) << "SignalEOS: result must not be OK";
    return;
  }
  std::scoped_lock l(lock_);
  eos_result_ = result;
  condition_.notify_all();
}

}  // namespace player
}  // namespace ave
