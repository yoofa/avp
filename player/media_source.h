/*
 * media_source.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef MEDIA_SOURCE_H
#define MEDIA_SOURCE_H

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>

#include "base/constructor_magic.h"
#include "base/types.h"
#include "common/buffer.h"
#include "player/player_interface.h"

namespace avp {
class MediaSource {
 public:
  struct ReadOptions {
    using SeekMode = PlayerBase::SeekMode;

    ReadOptions() { reset(); }

    // Reset everything back to defaults.
    void reset() {
      mOptions = 0;
      mSeekTimeUs = 0;
      mNonBlocking = false;
    }

    void setSeekTo(int64_t time_us,
                   SeekMode mode = SeekMode::SEEK_CLOSEST_SYNC);
    void clearSeekTo() {
      mOptions &= ~kSeekTo_Option;
      mSeekTimeUs = 0;
      mSeekMode = SeekMode::SEEK_CLOSEST_SYNC;
    }
    bool getSeekTo(int64_t* time_us, SeekMode* mode) const;

    void setNonBlocking();
    void clearNonBlocking();
    bool getNonBlocking() const;

    // Used to clear all non-persistent options for multiple buffer reads.
    void clearNonPersistent() { clearSeekTo(); }

   private:
    enum Options {
      kSeekTo_Option = 1,
    };

    uint32_t mOptions;
    int64_t mSeekTimeUs;
    SeekMode mSeekMode;
    bool mNonBlocking;
  } __attribute__((packed));  // sent through Binder

  MediaSource() = default;
  virtual ~MediaSource() = default;

  PlayerBase::media_track_type type() const { return mTrackType; }
  virtual bool supportReadMultiple() const { return false; }

  virtual status_t start() = 0;
  virtual status_t stop() = 0;
  virtual status_t read(std::shared_ptr<Buffer>& buffer,
                        const ReadOptions* options) = 0;
  virtual status_t readMultiple(std::vector<std::shared_ptr<Buffer>>& buffers,
                                uint32_t maxBuffers = 1,
                                const ReadOptions* options = nullptr) {
    return OK;
  }

 private:
  PlayerBase::media_track_type mTrackType;
  std::list<std::shared_ptr<Buffer>> mBuffers;
  std::mutex mLock;
  std::condition_variable mCondition;

  AVP_DISALLOW_COPY_AND_ASSIGN(MediaSource);
};
} /* namespace avp */

#endif /* !MEDIA_SOURCE_H */
