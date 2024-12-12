/*
 * default_content_source_factory.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_CONTENT_SOURCE_FACTORY_H
#define DEFAULT_CONTENT_SOURCE_FACTORY_H

#include "api/content_source_factory.h"

namespace avp {

class DefaultContentSourceFactory : public ContentSourceFactory {
 public:
  ~DefaultContentSourceFactory() = default;

  std::shared_ptr<ContentSource> CreateContentSource(
      const char* url,
      const std::unordered_map<std::string, std::string>& headers) override;

  std::shared_ptr<ContentSource> CreateContentSource(int fd,
                                                     int64_t offset,
                                                     int64_t length) override;

  std::shared_ptr<ContentSource> CreateContentSource(
      std::shared_ptr<ave::DataSource> data_source) override;
};

}  // namespace avp

#endif /* !DEFAULT_CONTENT_SOURCE_FACTORY_H */
