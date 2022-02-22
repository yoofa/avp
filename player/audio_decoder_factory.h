/*
 * audio_decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AUDIO_DECODER_FACTORY_H
#define AUDIO_DECODER_FACTORY_H

#include <memory>

#include "audio_decoder.h"

namespace avp {

class AudioDecoderFactory {
 protected:
  AudioDecoderFactory() = default;
  virtual ~AudioDecoderFactory() = default;

 public:
  virtual std::shared_ptr<AudioDecoder> createDecoder() = 0;
};

} /* namespace avp */
#endif /* !AUDIO_DECODER_FACTORY_H */
