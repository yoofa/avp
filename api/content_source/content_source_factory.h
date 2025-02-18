/*
 * content_source_factory.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef CONTENT_SOURCE_FACTORY_H
#define CONTENT_SOURCE_FACTORY_H

#include <memory>

#include "base/data_source/data_source.h"

#include "content_source.h"

namespace ave {
namespace player {

class ContentSourceFactory {
 public:
  virtual ~ContentSourceFactory() = default;

  virtual std::shared_ptr<ContentSource> CreateContentSource(
      const char* url,
      const std::unordered_map<std::string, std::string>& headers) = 0;

  virtual std::shared_ptr<ContentSource>
  CreateContentSource(int fd, int64_t offset, int64_t length) = 0;

  virtual std::shared_ptr<ContentSource> CreateContentSource(
      std::shared_ptr<ave::DataSource> data_source) = 0;
};

}  // namespace player
}  // namespace ave

#endif /* !CONTENT_SOURCE_FACTORY_H */
