/*
 * avp_builder.cc
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "api/player.h"
#include "player/avplayer.h"

namespace ave {
namespace player {

std::shared_ptr<Player> Player::Builder::build() {
  // Create AvPlayer instance and cast it to Player
  auto player = std::make_shared<AvPlayer>(
      std::move(content_source_factory_), std::move(demuxer_factory_),
      std::move(codec_factory_), std::move(audio_device_module_));
  return std::static_pointer_cast<Player>(player);
}

}  // namespace player
}  // namespace ave
