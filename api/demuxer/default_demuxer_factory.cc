/*
 * default_demuxer_factory.cc
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "default_demuxer_factory.h"

#include "base/logging.h"

namespace ave {
namespace player {

DefaultDemuxerFactory::DefaultDemuxerFactory(
    std::shared_ptr<DemuxerFactory> internal_factory,
    std::shared_ptr<DemuxerFactory> ffmpeg_factory)
    : internal_factory_(std::move(internal_factory)),
      ffmpeg_factory_(std::move(ffmpeg_factory)) {}

std::shared_ptr<Demuxer> DefaultDemuxerFactory::CreateDemuxer(
    std::shared_ptr<ave::DataSource> dataSource) {
  // Try internal (native) demuxers first
  if (internal_factory_) {
    auto demuxer = internal_factory_->CreateDemuxer(dataSource);
    if (demuxer) {
      AVE_LOG(LS_INFO) << "DefaultDemuxerFactory: using internal demuxer ("
                       << demuxer->name() << ")";
      return demuxer;
    }
  }

  // Fall back to FFmpeg
  if (ffmpeg_factory_) {
    auto demuxer = ffmpeg_factory_->CreateDemuxer(std::move(dataSource));
    if (demuxer) {
      AVE_LOG(LS_INFO) << "DefaultDemuxerFactory: using ffmpeg demuxer ("
                       << demuxer->name() << ")";
      return demuxer;
    }
  }

  AVE_LOG(LS_ERROR) << "DefaultDemuxerFactory: no demuxer could handle input";
  return nullptr;
}

}  // namespace player
}  // namespace ave
