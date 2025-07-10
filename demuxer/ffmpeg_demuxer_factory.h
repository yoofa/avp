/*
 * ffmpeg_demuxer_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DEMUXER_FACTORY_H
#define FFMPEG_DEMUXER_FACTORY_H

#include <memory>

#include "api/demuxer/demuxer.h"
#include "api/demuxer/demuxer_factory.h"
#include "base/data_source/data_source.h"

namespace ave {
namespace player {

class FFmpegDemuxerFactory : public DemuxerFactory {
 public:
  FFmpegDemuxerFactory() = default;
  ~FFmpegDemuxerFactory() override = default;

  std::shared_ptr<Demuxer> CreateDemuxer(
      std::shared_ptr<ave::DataSource> dataSource) override;
};

}  // namespace player
}  // namespace ave

#endif /* !FFMPEG_DEMUXER_FACTORY_H */
