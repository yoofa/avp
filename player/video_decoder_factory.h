/*
 * video_decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef VIDEO_DECODER_FACTORY_H
#define VIDEO_DECODER_FACTORY_H

#include <memory>

#include "base/types.h"
#include "player/video_decoder.h"

namespace avp {

class VideoDecoderFactory {
 protected:
  VideoDecoderFactory() = default;
  virtual ~VideoDecoderFactory() = default;

 public:
  virtual std::shared_ptr<VideoDecoder> createDecoder() = 0;
};

} /* namespace avp */
#endif /* !VIDEO_DECODER_FACTORY_H */
