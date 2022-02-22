/*
 * ffmpeg_demuxer_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DEMUXER_FACTORY_H
#define FFMPEG_DEMUXER_FACTORY_H

#include <memory>

#include "player/data_source.h"
#include "player/demuxer.h"
#include "player/demuxer_factory.h"

namespace avp {
class FFmpegDemuxerFactory : public DemuxerFactory {
 public:
  FFmpegDemuxerFactory();
  virtual ~FFmpegDemuxerFactory() override;
  virtual Demuxer* createDemuxer(
      std::shared_ptr<DataSource>& dataSource) override;

 private:
  void probe(std::shared_ptr<DataSource>& dataSource, int& score);
};
} /* namespace avp */

#endif /* !FFMPEG_DEMUXER_FACTORY_H */
