/*
 * default_demuxer_factory.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "default_demuxer_factory.h"

namespace avp {
DefaultDemuxerFactory::DefaultDemuxerFactory() {}
DefaultDemuxerFactory::~DefaultDemuxerFactory() {}
Demuxer* DefaultDemuxerFactory::createDemuxer(
    std::shared_ptr<DataSource>& dataSource) {
  return nullptr;
}
} /* namespace avp */
