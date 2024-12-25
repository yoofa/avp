/*
 * demuxer_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_FACTORY_H
#define DEMUXER_FACTORY_H

#include <memory>

#include "api/demuxer/demuxer.h"
#include "base/data_source/data_source.h"

namespace avp {

class DemuxerFactory {
 public:
  DemuxerFactory() = default;
  virtual ~DemuxerFactory() = default;

  virtual std::shared_ptr<Demuxer> CreateDemuxer(
      std::shared_ptr<ave::DataSource> dataSource) = 0;
};

} /* namespace avp */

#endif /* !DEMUXER_FACTORY_H */
