/*
 * decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DECODER_FACTORY_H
#define DECODER_FACTORY_H

#include <memory>

#include "decoder.h"

namespace avp {

class DecoderFactory {
 public:
  DecoderFactory() = default;
  virtual ~DecoderFactory() = default;

  virtual std::shared_ptr<Decoder> createDecoder(const char* mime) = 0;
};

} /* namespace avp */

#endif /* !DECODER_FACTORY_H */
