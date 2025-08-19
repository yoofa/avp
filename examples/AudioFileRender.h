/*
 * AudioFileRender.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AUDIOFILERENDER_H
#define AUDIOFILERENDER_H

#include "media/audio/audio_sink.h"
#include "media/foundation/buffer.h"

namespace ave {
namespace media {

class AudioFileRender : public AudioSink {
 public:
  AudioFileRender(const char* file);
  virtual ~AudioFileRender();
  virtual void onFrame(std::shared_ptr<Buffer>& frame) override;

 protected:
  int mFd;
};

}  // namespace media
}  // namespace ave

#endif /* !AUDIOFILERENDER_H */
