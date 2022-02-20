/*
 * Handler.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "handler.h"

namespace avp {

void Handler::deliverMessage(const std::shared_ptr<Message>& message) {
  onMessageReceived(message);
  mMessageCounter++;
}

}  // namespace avp
