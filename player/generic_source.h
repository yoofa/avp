/*
 * generic_source.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_GENERIC_SOURCE_H
#define AVP_GENERIC_SOURCE_H

#include <memory>

#include "common/message.h"
#include "player_interface.h"

namespace avp {

class GenericSource : public PlayerBase::ContentSource {
 public:
  virtual ~GenericSource() = default;

 private:
};

}  // namespace avp

#endif /* !AVP_GENERIC_SOURCE_H */
