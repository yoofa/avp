/*
 * demuxer_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_FACTORY_H
#define DEMUXER_FACTORY_H

#include <memory>

#include "player/data_source.h"
#include "player/demuxer.h"

namespace avp {
class DemuxerFactory {
 public:
  DemuxerFactory() = default;
  virtual ~DemuxerFactory() = default;

  virtual Demuxer* createDemuxer(std::shared_ptr<DataSource>& dataSource) = 0;
};
} /* namespace avp */

#endif /* !DEMUXER_FACTORY_H */
