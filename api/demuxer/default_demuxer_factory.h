/*
 * default_demuxer_factory.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_DEMUXER_FACTORY_H
#define DEFAULT_DEMUXER_FACTORY_H

#include <memory>

#include "demuxer_factory.h"

namespace ave {
namespace player {

// Composite factory that tries internal (native) demuxers first,
// then falls back to FFmpeg-based demuxers.
class DefaultDemuxerFactory : public DemuxerFactory {
 public:
  DefaultDemuxerFactory(std::shared_ptr<DemuxerFactory> internal_factory,
                        std::shared_ptr<DemuxerFactory> ffmpeg_factory);
  ~DefaultDemuxerFactory() override = default;

  std::shared_ptr<Demuxer> CreateDemuxer(
      std::shared_ptr<ave::DataSource> dataSource) override;

 private:
  std::shared_ptr<DemuxerFactory> internal_factory_;
  std::shared_ptr<DemuxerFactory> ffmpeg_factory_;
};

}  // namespace player
}  // namespace ave

#endif /* !DEFAULT_DEMUXER_FACTORY_H */
