/*
 * avp_builder.cc
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "api/player.h"

#include "avplayer.h"

#include "api/content_source/default_content_source_factory.h"
#include "demuxer/ffmpeg_demuxer_factory.h"
#include "media/audio/audio_device.h"
#include "media/codec/ffmpeg/ffmpeg_codec_factory.h"

namespace ave {
namespace player {

std::shared_ptr<Player> Player::Builder::build() {
  if (audio_device_ == nullptr) {
    audio_device_ = AudioDevice::CreateAudioDevice();
  }
  if (codec_factory_ == nullptr) {
    codec_factory_ = std::make_shared<media::FFmpegCodecFactory>();
  }

  if (demuxer_factory_ == nullptr) {
    demuxer_factory_ = std::make_shared<FFmpegDemuxerFactory>();
  }

  if (content_source_factory_ == nullptr) {
    content_source_factory_ =
        std::make_shared<DefaultContentSourceFactory>(demuxer_factory_);
  }

  // Create AvPlayer instance and cast it to Player
  auto player = std::make_shared<AvPlayer>(
      std::move(content_source_factory_), std::move(demuxer_factory_),
      std::move(codec_factory_), std::move(audio_device_));
  return std::static_pointer_cast<Player>(player);
}

}  // namespace player
}  // namespace ave
