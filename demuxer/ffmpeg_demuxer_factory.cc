/*
 * ffmpeg_demuxer_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_demuxer_factory.h"

#include <memory>

#include "ffmpeg_demuxer.h"

namespace ave {
namespace player {

std::shared_ptr<Demuxer> FFmpegDemuxerFactory::CreateDemuxer(
    std::shared_ptr<ave::DataSource> dataSource) {
  auto demuxer = std::make_shared<FFmpegDemuxer>(std::move(dataSource));
  if (demuxer->Init() != ave::OK) {
    return nullptr;
  }
  return demuxer;
}

}  // namespace player
}  // namespace ave
