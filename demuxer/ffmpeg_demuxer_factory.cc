/*
 * ffmpeg_demuxer_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_demuxer_factory.h"

#include <memory>

#include "base/logging.h"
#include "ffmpeg_demuxer.h"

namespace ave {
namespace player {

std::shared_ptr<Demuxer> FFmpegDemuxerFactory::CreateDemuxer(
    std::shared_ptr<DataSource> data_source) {
  return CreateDemuxer(std::move(data_source), {});
}

std::shared_ptr<Demuxer> FFmpegDemuxerFactory::CreateDemuxer(
    std::shared_ptr<DataSource> data_source,
    const std::vector<std::string>& preferred_container_mime_types) {
  (void)preferred_container_mime_types;
  const auto sniff_result = FFmpegDemuxer::Sniff(data_source);
  if (!sniff_result) {
    return nullptr;
  }

  auto demuxer = std::make_shared<FFmpegDemuxer>(std::move(data_source));
  if (demuxer->Init() != ave::OK) {
    return nullptr;
  }
  AVE_LOG(LS_VERBOSE) << "FFmpegDemuxerFactory: detected container "
                      << sniff_result->container_mime_type;
  return demuxer;
}

}  // namespace player
}  // namespace ave
