/*
 * internal_demuxer_factory.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "internal_demuxer_factory.h"

#include "base/logging.h"
#include "mp4_demuxer.h"
#include "mpeg2/mpeg2_ps_demuxer.h"
#include "mpeg2/mpeg2_ts_demuxer.h"

namespace ave {
namespace player {

std::shared_ptr<Demuxer> InternalDemuxerFactory::CreateDemuxer(
    std::shared_ptr<ave::DataSource> data_source) {
  auto mpeg2ts = std::make_shared<Mpeg2TsDemuxer>(data_source);
  status_t err = mpeg2ts->Init();
  if (err == OK) {
    AVE_LOG(LS_INFO) << "InternalDemuxerFactory: using Mpeg2TsDemuxer";
    return mpeg2ts;
  }

  auto mpeg2ps = std::make_shared<Mpeg2PsDemuxer>(data_source);
  err = mpeg2ps->Init();
  if (err == OK) {
    AVE_LOG(LS_INFO) << "InternalDemuxerFactory: using Mpeg2PsDemuxer";
    return mpeg2ps;
  }

  // Try Mp4Demuxer first
  auto mp4 = std::make_shared<Mp4Demuxer>(data_source);
  err = mp4->Init();
  if (err == OK) {
    AVE_LOG(LS_INFO) << "InternalDemuxerFactory: using Mp4Demuxer";
    return mp4;
  }

  // Add more internal demuxers here in the future (e.g. fMP4, TS, etc.)

  return nullptr;
}

}  // namespace player
}  // namespace ave
