/*
 * avplayer.cpp
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avplayer.h"

#include <iostream>

#include "base/error.h"
#include "common/message.h"

#include "generic_source.h"

namespace avp {

AvPlayer::AvPlayer() : mPlayerLooper(std::make_shared<Looper>()) {
  mPlayerLooper->setName("AvPlayer");
}

AvPlayer::~AvPlayer() {}

status_t AvPlayer::init() {
  mPlayerLooper->start();
  mPlayerLooper->registerHandler(shared_from_this());
  return 0;
}

status_t AvPlayer::setDataSource(int fd, int64_t offset, int64_t length) {
  auto msg = std::make_shared<Message>(kWhatSetDataSource, shared_from_this());
  auto notify =
      std::make_shared<Message>(kWhatSourceNotify, shared_from_this());

  // std::shared_ptr<GenericSource> source =
  //     std::make_shared<GenericSource>(notify);

  return 0;
}

status_t AvPlayer::prepare() {
  return 0;
}

status_t AvPlayer::start() {
  return 0;
}

status_t AvPlayer::stop() {
  return 0;
}

status_t AvPlayer::pause() {
  return 0;
}

status_t AvPlayer::resume() {
  return 0;
}

status_t AvPlayer::seekTo(int msec, SeekMode seekMode) {
  return 0;
}

status_t AvPlayer::reset() {
  return 0;
}

void AvPlayer::onMessageReceived(const std::shared_ptr<Message>& message) {
  switch (message->what()) {
    default:
      break;
  }
}

}  // namespace avp
