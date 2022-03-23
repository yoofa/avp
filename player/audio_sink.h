/*
 * audio_sink.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#include <memory>

#include "common/message.h"
#include "player/audio_frame.h"

namespace avp {
class AudioSink : public MessageObject {
 public:
  AudioSink() = default;
  virtual ~AudioSink() = default;

  virtual void onFrame(std::shared_ptr<Buffer>& frame) = 0;
};
} /* namespace avp */

#endif /* !AUDIO_SINK_H */
