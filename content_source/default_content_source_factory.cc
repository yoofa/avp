/*
 * default_content_source_factory.cc
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "default_content_source_factory.h"

namespace avp {

std::shared_ptr<ContentSource> DefaultContentSourceFactory::CreateContentSource(
    const char* url,
    const std::unordered_map<std::string, std::string>& headers) {
  return nullptr;
}

std::shared_ptr<ContentSource> DefaultContentSourceFactory::CreateContentSource(
    int fd,
    int64_t offset,
    int64_t length) {
  return nullptr;
}

std::shared_ptr<ContentSource> DefaultContentSourceFactory::CreateContentSource(
    std::shared_ptr<ave::DataSource> data_source) {
  return nullptr;
}

}  // namespace avp
