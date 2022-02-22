/*
 * default_video_decoder_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include <memory>

#include "player/default_video_decoder_factory.h"
#include "player/video_decoder.h"

namespace avp {

DefaultVideoDecoderFactory::DefaultVideoDecoderFactory() {}
DefaultVideoDecoderFactory::~DefaultVideoDecoderFactory() {}

std::shared_ptr<VideoDecoder> DefaultVideoDecoderFactory::createDecoder() {
  return nullptr;
}

} /* namespace avp */
