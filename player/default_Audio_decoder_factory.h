/*
 * default_Audio_decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_AUDIO_DECODER_FACTORY_H
#define DEFAULT_AUDIO_DECODER_FACTORY_H

#include <memory>

#include "player/audio_decoder.h"
#include "player/audio_decoder_factory.h"

namespace avp {

class DefaultAudioDecoderFactory : public AudioDecoderFactory {
 public:
  DefaultAudioDecoderFactory();
  virtual ~DefaultAudioDecoderFactory() override;
  virtual std::shared_ptr<AudioDecoder> createDecoder() override;
};

} /* namespace avp */

#endif /* !DEFAULT_AUDIO_DECODER_FACTORY_H */
