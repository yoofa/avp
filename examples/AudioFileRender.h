/*
 * AudioFileRender.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AUDIOFILERENDER_H
#define AUDIOFILERENDER_H

#include "player/audio_sink.h"

namespace ave {
namespace player {

class AudioFileRender : public AudioSink {
 public:
  AudioFileRender(const char* file);
  virtual ~AudioFileRender();
  virtual void onFrame(std::shared_ptr<Buffer>& frame) override;

 protected:
  int mFd;
};

}  // namespace player
}  // namespace ave

#endif /* !AUDIOFILERENDER_H */
