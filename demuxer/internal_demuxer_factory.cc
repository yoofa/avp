/*
 * internal_demuxer_factory.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "internal_demuxer_factory.h"

#include "base/logging.h"
#include "mp4_demuxer.h"

namespace ave {
namespace player {

std::shared_ptr<Demuxer> InternalDemuxerFactory::CreateDemuxer(
    std::shared_ptr<ave::DataSource> data_source) {
  // Try Mp4Demuxer first
  auto mp4 = std::make_shared<Mp4Demuxer>(data_source);
  if (mp4->Init() == OK) {
    AVE_LOG(LS_INFO) << "InternalDemuxerFactory: using Mp4Demuxer";
    return mp4;
  }

  // Add more internal demuxers here in the future (e.g. fMP4, TS, etc.)

  return nullptr;
}

}  // namespace player
}  // namespace ave
