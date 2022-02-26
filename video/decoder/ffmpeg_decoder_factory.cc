/*
 * ffmpeg_decoder_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "ffmpeg_decoder_factory.h"

#include <memory>

#include "base/logging.h"
#include "video/decoder/ffmpeg_decoder.h"

namespace avp {

FFmpegDecoderFactory::FFmpegDecoderFactory() {}
FFmpegDecoderFactory::~FFmpegDecoderFactory() {}

std::shared_ptr<Decoder> FFmpegDecoderFactory::createDecoder(const char* name) {
  LOG(LS_INFO) << "createDecoder " << name;
  return nullptr;
}

} /* namespace avp */
