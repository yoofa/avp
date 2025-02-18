/*
 * ffmpeg_demuxer_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_demuxer_factory.h"

#include "ffmpeg_demuxer.h"

namespace ave {
namespace player {

FFmpegDemuxerFactory::FFmpegDemuxerFactory() {}
FFmpegDemuxerFactory::~FFmpegDemuxerFactory() {}

std::shared_ptr<Demuxer> FFmpegDemuxerFactory::createDemuxer(
    std::shared_ptr<DataSource> dataSource) {
  std::shared_ptr<FFmpegDemuxer> ffmpegDemuxer(
      std::make_shared<FFmpegDemuxer>(dataSource));
  ffmpegDemuxer->init();
  return ffmpegDemuxer;
}

}  // namespace player
}  // namespace ave
