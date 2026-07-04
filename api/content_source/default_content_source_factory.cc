/*
 * default_content_source_factory.cc
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "default_content_source_factory.h"

#include <algorithm>
#include <cctype>
#include <memory>

#include "api/demuxer/demuxer_factory.h"
#include "base/data_source/http_source.h"
#include "content_source/generic_source.h"
#include "content_source/http_live/http_live_source.h"
#include "content_source/http_live/playlist_parser.h"
#include "content_source/rtsp/rtsp_source.h"

namespace ave {
namespace player {
using ave::base::HTTPSource;

namespace {

std::string ToLower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool IsHttpUrl(const std::string& url) {
  const std::string lower = ToLower(url);
  return lower.starts_with("http://") || lower.starts_with("https://");
}

bool IsRtspUrl(const std::string& url) {
  return ToLower(url).starts_with("rtsp://");
}

std::shared_ptr<DataSource> CreateHttpDataSource(
    const std::shared_ptr<HTTPProvider>& http_provider,
    const char* url,
    const std::unordered_map<std::string, std::string>& headers) {
  if (!http_provider || !url) {
    return nullptr;
  }
  auto connection = http_provider->CreateConnection();
  if (!connection) {
    return nullptr;
  }
  auto source = std::make_shared<HTTPSource>(connection);
  if (source->Connect(url, headers, 0) != OK) {
    return nullptr;
  }
  return source;
}

}  // namespace

std::shared_ptr<ContentSource> DefaultContentSourceFactory::CreateContentSource(
    const char* url,
    const std::unordered_map<std::string, std::string>& headers) {
  if (url && IsRtspUrl(url)) {
    auto source = std::make_shared<RTSPSource>();
    if (source->SetDataSource(url, headers) != OK) {
      return nullptr;
    }
    return source;
  }

  if (url && IsHttpUrl(url) && http_live::LooksLikeHlsUrl(url)) {
    auto source = std::make_shared<HttpLiveSource>(http_provider_);
    if (source->SetDataSource(url, headers) != OK) {
      return nullptr;
    }
    return source;
  }

  if (url && IsHttpUrl(url)) {
    auto data_source = CreateHttpDataSource(http_provider_, url, headers);
    if (!data_source) {
      return nullptr;
    }
    auto source = std::make_shared<GenericSource>(demuxer_factory_);
    source->SetDataSource(std::move(data_source));
    return source;
  }

  auto source = std::make_shared<GenericSource>(demuxer_factory_);
  source->SetDataSource(url);
  return source;
}

std::shared_ptr<ContentSource> DefaultContentSourceFactory::CreateContentSource(
    int fd,
    int64_t offset,
    int64_t length) {
  auto source = std::make_shared<GenericSource>(demuxer_factory_);
  source->SetDataSource(fd, offset, length);
  return source;
}

std::shared_ptr<ContentSource> DefaultContentSourceFactory::CreateContentSource(
    std::shared_ptr<DataSource> data_source) {
  auto source = std::make_shared<GenericSource>(demuxer_factory_);
  source->SetDataSource(std::move(data_source));
  return source;
}

}  // namespace player
}  // namespace ave
