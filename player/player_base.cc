/*
 * player_base.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "base/logging.h"
#include "media/utils.h"
#include "player/player_interface.h"

namespace avp {

std::shared_ptr<Message> PlayerBase::ContentSource::getFormat(bool audio) {
  std::shared_ptr<MetaData> meta = getMeta(audio);
  if (meta.get() == nullptr) {
    return nullptr;
  }
  std::shared_ptr<Message> msg = std::make_shared<Message>();

  if (convertMetaDataToMessage(meta, msg) == OK) {
    return msg;
  }

  return nullptr;
}

void PlayerBase::ContentSource::notifyFlagsChanged(uint32_t flags) {
  std::shared_ptr<Message> notify = dupNotify();
  notify->setInt32("what", kWhatFlagsChanged);
  notify->setInt32("flags", flags);
  notify->post(0);
}

void PlayerBase::ContentSource::notifyVideoSizeChanged(
    const std::shared_ptr<Message> format) {
  std::shared_ptr<Message> notify = dupNotify();
  notify->setInt32("what", kWhatVideoSizeChanged);
  notify->setMessage("format", std::move(format));
  notify->post(0);
}

void PlayerBase::ContentSource::notifyPrepared(status_t err) {
  LOG(LS_VERBOSE) << "Source::notifyPrepared " << err;
  std::shared_ptr<Message> notify = dupNotify();
  notify->setInt32("what", kWhatPrepared);
  notify->setInt32("err", err);
  notify->post(0);
}

} /* namespace avp */
