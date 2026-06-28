/*
 * demuxer_factory.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_FACTORY_H
#define DEMUXER_FACTORY_H

#include <memory>
#include <string>
#include <vector>

#include "api/demuxer/demuxer.h"
#include "base/data_source/data_source.h"

namespace ave {
namespace player {

using ave::base::DataSource;

class DemuxerFactory {
 public:
  DemuxerFactory() = default;
  virtual ~DemuxerFactory() = default;

  virtual std::shared_ptr<Demuxer> CreateDemuxer(
      std::shared_ptr<DataSource> dataSource) = 0;

  // |preferred_container_mime_types| orders demuxer probing but never limits
  // fallback probing. The default preserves compatibility with factories that
  // only implement the original single-argument API.
  virtual std::shared_ptr<Demuxer> CreateDemuxer(
      std::shared_ptr<DataSource> data_source,
      const std::vector<std::string>& preferred_container_mime_types) {
    (void)preferred_container_mime_types;
    return CreateDemuxer(std::move(data_source));
  }
};

}  // namespace player
}  // namespace ave

#endif /* !DEMUXER_FACTORY_H */
