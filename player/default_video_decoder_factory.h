/*
 * default_video_decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_VIDEO_DECODER_FACTORY_H
#define DEFAULT_VIDEO_DECODER_FACTORY_H

#include <memory>

#include "player/video_decoder.h"
#include "player/video_decoder_factory.h"

namespace avp {
class DefaultVideoDecoderFactory : public VideoDecoderFactory {
 public:
  DefaultVideoDecoderFactory();
  virtual ~DefaultVideoDecoderFactory() override;
  virtual std::shared_ptr<VideoDecoder> createDecoder() override;
};
} /* namespace avp */

#endif /* !DEFAULT_VIDEO_DECODER_FACTORY_H */
