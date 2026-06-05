/*
 * default_content_source_factory.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEFAULT_CONTENT_SOURCE_FACTORY_H
#define DEFAULT_CONTENT_SOURCE_FACTORY_H

#include "base/net/http/http_provider.h"

#include "api/content_source/content_source_factory.h"

namespace ave {
namespace player {

using ave::base::DataSource;
using ave::base::net::HTTPProvider;

class DemuxerFactory;

// A concrete factory that produces GenericSource-backed ContentSource
// and injects a DemuxerFactory so GenericSource can demux input.
class DefaultContentSourceFactory : public ContentSourceFactory {
 public:
  explicit DefaultContentSourceFactory(
      std::shared_ptr<DemuxerFactory> demuxer_factory,
      std::shared_ptr<HTTPProvider> http_provider = nullptr)
      : demuxer_factory_(std::move(demuxer_factory)),
        http_provider_(std::move(http_provider)) {}
  ~DefaultContentSourceFactory() override = default;

  std::shared_ptr<ContentSource> CreateContentSource(
      const char* url,
      const std::unordered_map<std::string, std::string>& headers) override;

  std::shared_ptr<ContentSource> CreateContentSource(int fd,
                                                     int64_t offset,
                                                     int64_t length) override;

  std::shared_ptr<ContentSource> CreateContentSource(
      std::shared_ptr<DataSource> data_source) override;

 private:
  std::shared_ptr<DemuxerFactory> demuxer_factory_;
  std::shared_ptr<HTTPProvider> http_provider_;
};

}  // namespace player
}  // namespace ave

#endif /* !DEFAULT_CONTENT_SOURCE_FACTORY_H */
