/*
 * ffmpeg_demuxer_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_demuxer_factory.h"

#include "ffmpeg_demuxer.h"

namespace avp {
FFmpegDemuxerFactory::FFmpegDemuxerFactory() {}
FFmpegDemuxerFactory::~FFmpegDemuxerFactory() {}

Demuxer* FFmpegDemuxerFactory::createDemuxer(
    std::shared_ptr<DataSource>& dataSource) {
  return new FFmpegDemuxer();
}

} /* namespace avp */
