/*
 * internal_demuxer_factory.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_INTERNAL_DEMUXER_FACTORY_H_
#define DEMUXER_INTERNAL_DEMUXER_FACTORY_H_

#include "api/demuxer/demuxer_factory.h"

namespace ave {
namespace player {

// Factory for demuxers implemented natively in AVP source code
// (as opposed to FFmpeg-based demuxers). Tries each internal demuxer
// in order until one succeeds.
class InternalDemuxerFactory : public DemuxerFactory {
 public:
  InternalDemuxerFactory() = default;
  ~InternalDemuxerFactory() override = default;

  std::shared_ptr<Demuxer> CreateDemuxer(
      std::shared_ptr<ave::DataSource> data_source) override;
};

}  // namespace player
}  // namespace ave

#endif  // DEMUXER_INTERNAL_DEMUXER_FACTORY_H_
