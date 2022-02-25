/*
 * default_demuxer_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_DEMUXER_FACTORY_H
#define DEFAULT_DEMUXER_FACTORY_H

#include <memory>

#include "player/data_source.h"
#include "player/demuxer.h"
#include "player/demuxer_factory.h"

namespace avp {
class DefaultDemuxerFactory : public DemuxerFactory {
 public:
  DefaultDemuxerFactory();
  virtual ~DefaultDemuxerFactory();
  std::shared_ptr<Demuxer> createDemuxer(
      std::shared_ptr<DataSource> dataSource) override;
};
} /* namespace avp */

#endif /* !DEFAULT_DEMUXER_FACTORY_H */
