/*
 * packet_source.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PACKET_SOURCE_H
#define PACKET_SOURCE_H

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>

#include "api/player_interface.h"
#include "base/constructor_magic.h"
#include "media/foundation/media_packet.h"

namespace avp {

using ave::media::MediaPacket;

class PacketSource {
 public:
  PacketSource(media_track_type track_type = MEDIA_TRACK_TYPE_UNKNOWN);
  ~PacketSource();

  media_track_type type() const { return track_type_; }

  status_t Start();
  status_t Stop();

  void Clear();

  bool HasBufferAvailable(status_t* result);
  size_t GetAvailableBufferCount(status_t* result);

  status_t QueueAccessunit(std::shared_ptr<MediaPacket> packet);
  status_t DequeueAccessUnit(std::shared_ptr<MediaPacket>& packet);

 private:
  media_track_type track_type_;
  std::queue<std::shared_ptr<MediaPacket>> packets_;
  std::mutex lock_;
  std::condition_variable condition_;

  AVE_DISALLOW_COPY_AND_ASSIGN(PacketSource);
};
} /* namespace avp */

#endif /* !PACKET_SOURCE_H */
