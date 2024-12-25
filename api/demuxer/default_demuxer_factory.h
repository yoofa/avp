/*
 * default_demuxer_factory.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_DEMUXER_FACTORY_H
#define DEFAULT_DEMUXER_FACTORY_H

#include "demuxer_factory.h"

namespace avp {

class DefaultDemuxerFactory : public DemuxerFactory {
 public:
  DefaultDemuxerFactory() = default;
  ~DefaultDemuxerFactory() override = default;

  std::shared_ptr<Demuxer> CreateDemuxer(
      std::shared_ptr<ave::DataSource> dataSource) override;
};

}  // namespace avp

#endif /* !DEFAULT_DEMUXER_FACTORY_H */
