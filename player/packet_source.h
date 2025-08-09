/*
 * packet_source.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PACKET_SOURCE_H
#define PACKET_SOURCE_H

#include <condition_variable>
#include <memory>
#include <mutex>

#include "base/constructor_magic.h"
#include "base/thread_annotation.h"
#include "media/foundation/media_format.h"
#include "media/foundation/media_packet.h"
#include "media/foundation/media_utils.h"

namespace ave {
namespace player {

using ave::media::MediaFormat;
using ave::media::MediaPacket;
using ave::media::MediaType;

class PacketSource {
 public:
  explicit PacketSource(std::shared_ptr<MediaFormat> format);
  ~PacketSource();

  MediaType type() const {
    std::lock_guard<std::mutex> l(lock_);
    return format_->stream_type();
  }

  status_t Start();
  status_t Stop();

  void Clear();

  void SetFormat(std::shared_ptr<MediaFormat> format);

  bool HasBufferAvailable(status_t* result);
  size_t GetAvailableBufferCount(status_t* result);

  status_t QueueAccessunit(std::shared_ptr<MediaPacket> packet);
  status_t DequeueAccessUnit(std::shared_ptr<MediaPacket>& packet);

 private:
  mutable std::mutex lock_;
  std::condition_variable condition_;
  std::shared_ptr<MediaFormat> format_ GUARDED_BY(lock_);
  std::queue<std::shared_ptr<MediaPacket>> packets_ /*GUARDED_BY(lock_)*/;

  AVE_DISALLOW_COPY_AND_ASSIGN(PacketSource);
};

}  // namespace player
}  // namespace ave

#endif /* !PACKET_SOURCE_H */
