/*
 * audio_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include "base/system/ave_export.h"

namespace avp {

AVE_EXPORT class AudioDecoder {
 protected:
  AudioDecoder() = delete;
  virtual ~AudioDecoder() = delete;

 public:
};

}  // namespace avp

#endif /* !AUDIO_DECODER_H */
