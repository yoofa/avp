/*
 * av_play.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "av_play.h"

#include <memory>

#include "player/avplayer.h"

using avp::AvPlayer;

int main(int args, char* argv[]) {
  std::shared_ptr<AvPlayer> mPlayer = std::make_shared<AvPlayer>();
  mPlayer->init();

  return 0;
}
