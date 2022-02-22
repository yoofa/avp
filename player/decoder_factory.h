/*
 * decoder_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DECODER_FACTORY_H
#define DECODER_FACTORY_H

#include <memory>

#include "avp_decoder.h"

namespace avp {

class DecoderFactory {
 public:
  DecoderFactory();
  virtual ~DecoderFactory();

  virtual std::shared_ptr<AvpDecoder> createDecoder();
};

} /* namespace avp */

#endif /* !DECODER_FACTORY_H */