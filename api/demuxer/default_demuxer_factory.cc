/*
 * default_demuxer_factory.cc
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "default_demuxer_factory.h"

namespace ave {
namespace player {

std::shared_ptr<Demuxer> CreateDemuxer(
    std::shared_ptr<ave::DataSource> dataSource) {
  return nullptr;
}

}  // namespace player
}  // namespace ave
