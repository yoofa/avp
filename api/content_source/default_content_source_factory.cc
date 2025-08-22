/*
 * default_content_source_factory.cc
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "default_content_source_factory.h"

#include "content_source/generic_source.h"

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
  auto source = std::make_shared<GenericSource>();
  source->SetDataSource(fd, offset, length);
  return source;
}

std::shared_ptr<ContentSource> DefaultContentSourceFactory::CreateContentSource(
    std::shared_ptr<ave::DataSource> data_source) {
  auto source = std::make_shared<GenericSource>();
  source->SetDataSource(data_source);
  return source;
}

}  // namespace avp
