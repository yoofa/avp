/*
 * default_decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_DECODER_FACTORY_H
#define DEFAULT_DECODER_FACTORY_H

#include "media/media_defs.h"
#include "player/decoder.h"
#include "player/decoder_factory.h"

namespace avp {
class DefaultDecoderFactory : public DecoderFactory {
 public:
  DefaultDecoderFactory();
  virtual ~DefaultDecoderFactory();
  virtual std::shared_ptr<Decoder> createDecoder(bool audio,
                                                 CodecType type) override;
};

} /* namespace avp */

#endif /* !DEFAULT_DECODER_FACTORY_H */
