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
#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"
#include "media/foundation/media_utils.h"

namespace ave {
namespace player {

using ave::media::MediaFrame;
using ave::media::MediaMeta;
using ave::media::MediaType;

class PacketSource {
 public:
  explicit PacketSource(std::shared_ptr<MediaMeta> format);
  ~PacketSource();

  MediaType type() const {
    std::scoped_lock l(lock_);
    return format_->stream_type();
  }

  static status_t Start();
  static status_t Stop();

  void Clear();

  void SetFormat(std::shared_ptr<MediaMeta> format);

  bool HasBufferAvailable(status_t* result);
  size_t GetAvailableBufferCount(status_t* result);

  status_t QueueAccessunit(std::shared_ptr<MediaFrame> packet);
  status_t DequeueAccessUnit(std::shared_ptr<MediaFrame>& packet)
      NO_THREAD_SAFETY_ANALYSIS;

  void SignalEOS(status_t result);

 private:
  mutable std::mutex lock_;
  std::condition_variable condition_;
  std::shared_ptr<MediaMeta> format_ GUARDED_BY(lock_);
  std::queue<std::shared_ptr<MediaFrame>> packets_ /*GUARDED_BY(lock_)*/;
  status_t eos_result_ GUARDED_BY(lock_) = ave::OK;

  AVE_DISALLOW_COPY_AND_ASSIGN(PacketSource);
};

}  // namespace player
}  // namespace ave

#endif /* !PACKET_SOURCE_H */
