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
  int64_t average_bandwidth_bps = -1;
  std::string codecs;
  std::string audio_group_id;
};

struct MediaRendition {
  std::string group_id;
  std::string uri;
  bool is_default = false;
  bool autoselect = false;
};

struct MediaPlaylistSegment {
  std::string uri;
  int64_t duration_us = 0;
  int32_t sequence = 0;
  bool discontinuity = false;
  int64_t byterange_offset = -1;
  int64_t byterange_length = -1;
};

struct InitSegmentInfo {
  std::string uri;
  int64_t byterange_offset = -1;
  int64_t byterange_length = -1;
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

  InitSegmentInfo init_segment;
  std::vector<VariantPlaylistItem> variants;
  std::vector<MediaRendition> audio_renditions;
  std::vector<MediaPlaylistSegment> segments;
};

bool LooksLikeHlsUrl(const std::string& url);
std::string ResolveUri(const std::string& base_uri, const std::string& ref_uri);
status_t ParsePlaylist(const std::string& base_uri,
                       const std::string& playlist_text,
                       Playlist& playlist);
const VariantPlaylistItem* SelectPrimaryVariant(const Playlist& playlist);
const MediaRendition* SelectAudioRendition(const Playlist& playlist,
                                           const std::string& group_id);

}  // namespace http_live
}  // namespace player
}  // namespace ave

#endif  // AVP_CONTENT_SOURCE_HTTP_LIVE_PLAYLIST_PARSER_H_
