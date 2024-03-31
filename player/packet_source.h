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

#include "base/constructor_magic.h"
#include "base/types.h"
#include "media/buffer.h"
#include "player/player_interface.h"

namespace avp {

class PacketSource {
 public:
  PacketSource();
  ~PacketSource();

  PlayerBase::media_track_type type() const { return mTrackType; }

  status_t start();
  status_t stop();

  void clear();

  bool hasBufferAvailable(status_t* result);
  size_t getAvailableBufferCount(status_t* result);

  status_t queueAccessunit(std::shared_ptr<Buffer> buffer);
  status_t dequeueAccessUnit(std::shared_ptr<Buffer>& buffer);

 private:
  PlayerBase::media_track_type mTrackType;
  std::queue<std::shared_ptr<Buffer>> mBuffers;
  std::mutex mLock;
  std::condition_variable mCondition;

  AVP_DISALLOW_COPY_AND_ASSIGN(PacketSource);
};
} /* namespace avp */

#endif /* !PACKET_SOURCE_H */
