/*
 * player.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PLAYER_H
#define PLAYER_H

#include <memory>

#include "base/errors.h"
#include "media/audio/audio_device_factory.h"
#include "media/codec/codec_factory.h"

#include "content_source.h"
#include "player_interface.h"

namespace avp {
using ave::status_t;

class Player {
 public:
  class Builder {
   public:
    Builder() = default;
    ~Builder() = default;

    Builder& setCodecFactory(
        std::unique_ptr<ave::media::CodecFactory>&& codec_factory) {
      codec_factory_ = std::move(codec_factory);
      return *this;
    }

    Builder& setAudioDeviceFactory(
        std::unique_ptr<ave::media::AudioDeviceFactory>&&
            audio_device_factory) {
      audio_device_factory_ = std::move(audio_device_factory);
      return *this;
    }

    std::shared_ptr<Player> build();

   private:
    std::unique_ptr<ave::media::CodecFactory> codec_factory_;
    std::unique_ptr<ave::media::AudioDeviceFactory> audio_device_factory_;
  };

  class Listener {
   protected:
    Listener() = default;
    virtual ~Listener() = default;

   public:
  };

  Player() = default;
  virtual ~Player() = default;

  virtual status_t Init() = 0;
  virtual status_t SetListener(const std::shared_ptr<Listener>& listener) = 0;

  virtual status_t SetDataSource(const char* url) = 0;
  virtual status_t SetDataSource(int fd, int64_t offset, int64_t length) = 0;
  virtual status_t SetDataSource(
      const std::shared_ptr<ContentSource>& source) = 0;

  virtual status_t Prepare() = 0;
  virtual status_t Start() = 0;
  virtual status_t Stop() = 0;
  virtual status_t Pause() = 0;
  virtual status_t Resume() = 0;
  virtual status_t SeekTo(int msec, SeekMode mode) = 0;
  status_t SeekTo(int msec) {
    return SeekTo(msec, SeekMode::SEEK_PREVIOUS_SYNC);
  }
  virtual status_t Reset() = 0;

 protected:
  std::weak_ptr<Listener>& listener() { return listener_; }

 private:
  std::weak_ptr<Listener> listener_;
};

}  // namespace avp

#endif /* !PLAYER_H */
