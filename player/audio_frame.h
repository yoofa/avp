/*
 * audio_frame.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AUDIO_FRAME_H
#define AUDIO_FRAME_H

#include "media/foundation/message.h"

namespace avp {

class AudioFrame : public ave::MessageObject {
 public:
  AudioFrame();
  virtual ~AudioFrame();
};
} /* namespace avp */

#endif /* !AUDIO_FRAME_H */
