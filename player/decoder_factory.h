/*
 * decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DECODER_FACTORY_H
#define DECODER_FACTORY_H

#include <memory>

#include "common/media_defs.h"
#include "decoder.h"

namespace avp {

class DecoderFactory {
 public:
  DecoderFactory() = default;
  virtual ~DecoderFactory() = default;

  virtual std::shared_ptr<Decoder> createDecoder(bool audio,
                                                 CodecType type) = 0;
};

} /* namespace avp */

#endif /* !DECODER_FACTORY_H */
