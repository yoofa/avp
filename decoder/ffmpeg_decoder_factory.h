/*
 * ffmpeg_decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DECODER_FACTORY_H
#define FFMPEG_DECODER_FACTORY_H

#include <memory>

#include "player/decoder.h"
#include "player/decoder_factory.h"

namespace avp {
class FFmpegDecoderFactory : public DecoderFactory {
 public:
  FFmpegDecoderFactory();
  virtual ~FFmpegDecoderFactory();

  virtual std::shared_ptr<Decoder> createDecoder(bool audio,
                                                 CodecType codecType) override;
};

} /* namespace avp */

#endif /* !FFMPEG_DECODER_FACTORY_H */
