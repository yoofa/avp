/*
 * default_Audio_decoder_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include <memory>

#include "player/audio_decoder.h"
#include "player/default_Audio_decoder_factory.h"

namespace avp {

DefaultAudioDecoderFactory::DefaultAudioDecoderFactory() {}

DefaultAudioDecoderFactory::~DefaultAudioDecoderFactory() {}

std::shared_ptr<AudioDecoder> DefaultAudioDecoderFactory::createDecoder() {
  return nullptr;
}

} /* namespace avp */
