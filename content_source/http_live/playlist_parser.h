/*
 * playlist_parser.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_CONTENT_SOURCE_HTTP_LIVE_PLAYLIST_PARSER_H_
#define AVP_CONTENT_SOURCE_HTTP_LIVE_PLAYLIST_PARSER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/errors.h"

namespace ave {
namespace player {
namespace http_live {

struct VariantPlaylistItem {
  std::string uri;
  int64_t bandwidth_bps = -1;
};

struct MediaPlaylistSegment {
  std::string uri;
  int64_t duration_us = 0;
  int32_t sequence = 0;
  bool discontinuity = false;
};

struct Playlist {
  bool is_master = false;
  bool is_live = false;
  bool is_event = false;
  bool end_list = false;
  bool has_init_segment = false;
  bool is_encrypted = false;
  int64_t target_duration_us = -1;
  int64_t duration_us = -1;
  int32_t media_sequence = 0;

  std::vector<VariantPlaylistItem> variants;
  std::vector<MediaPlaylistSegment> segments;
};

bool LooksLikeHlsUrl(const std::string& url);
std::string ResolveUri(const std::string& base_uri, const std::string& ref_uri);
status_t ParsePlaylist(const std::string& base_uri,
                       const std::string& playlist_text,
                       Playlist& playlist);
const VariantPlaylistItem* SelectPrimaryVariant(const Playlist& playlist);

}  // namespace http_live
}  // namespace player
}  // namespace ave

#endif  // AVP_CONTENT_SOURCE_HTTP_LIVE_PLAYLIST_PARSER_H_
