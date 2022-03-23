/*
 * default_decoder_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "default_decoder_factory.h"

namespace avp {

DefaultDecoderFactory::DefaultDecoderFactory() {}
DefaultDecoderFactory::~DefaultDecoderFactory() {}

std::shared_ptr<Decoder> DefaultDecoderFactory::createDecoder(
    bool audio,
    CodecType codecType) {
  return nullptr;
}

}  // namespace avp
