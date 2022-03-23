/*
 * ffmpeg_decoder_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "ffmpeg_decoder_factory.h"

#include <memory>

#include "base/logging.h"
#include "decoder/ffmpeg_audio_decoder.h"
#include "decoder/ffmpeg_decoder.h"
#include "decoder/ffmpeg_video_decoder.h"

namespace avp {

FFmpegDecoderFactory::FFmpegDecoderFactory() {}
FFmpegDecoderFactory::~FFmpegDecoderFactory() {}

static bool codecSupported(CodecType codecType) {
  return true;
}

std::shared_ptr<Decoder> FFmpegDecoderFactory::createDecoder(
    bool audio,
    CodecType codecType) {
  LOG(LS_INFO) << "createDecoder " << codecType;
  if (!codecSupported(codecType)) {
    return nullptr;
  }

  if (audio) {
    return std::make_shared<FFmpegAudioDecoder>(codecType);
  } else {
    return std::make_shared<FFmpegVideoDecoder>(codecType);
  }
}

} /* namespace avp */
