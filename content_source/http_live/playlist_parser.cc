/*
 * playlist_parser.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "content_source/http_live/playlist_parser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace ave {
namespace player {
namespace http_live {

namespace {

std::string Trim(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string GetScheme(const std::string& uri) {
  size_t colon = uri.find(':');
  if (colon == std::string::npos) {
    return {};
  }
  for (size_t i = 0; i < colon; ++i) {
    unsigned char c = static_cast<unsigned char>(uri[i]);
    if (!std::isalpha(c) && !std::isdigit(c) && c != '+' && c != '-' &&
        c != '.') {
      return {};
    }
  }
  return ToLower(uri.substr(0, colon));
}

std::unordered_map<std::string, std::string> ParseAttributes(
    const std::string& text) {
  std::unordered_map<std::string, std::string> attrs;
  std::string current;
  bool in_quotes = false;
  std::vector<std::string> items;
  for (char c : text) {
    if (c == '"') {
      in_quotes = !in_quotes;
    }
    if (c == ',' && !in_quotes) {
      items.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  if (!current.empty()) {
    items.push_back(current);
  }

  for (const std::string& item : items) {
    size_t eq = item.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = ToLower(Trim(item.substr(0, eq)));
    std::string value = Trim(item.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    attrs[key] = value;
  }
  return attrs;
}

bool ParseDurationUs(const std::string& value, int64_t* duration_us) {
  if (!duration_us) {
    return false;
  }
  char* end = nullptr;
  double seconds = std::strtod(value.c_str(), &end);
  if (end == value.c_str()) {
    return false;
  }
  *duration_us = static_cast<int64_t>(seconds * 1000000.0);
  return true;
}

bool ParseInt64(const std::string& value, int64_t* out) {
  if (!out) {
    return false;
  }
  char* end = nullptr;
  long long parsed = std::strtoll(value.c_str(), &end, 10);
  if (end == value.c_str()) {
    return false;
  }
  *out = static_cast<int64_t>(parsed);
  return true;
}

bool ParseInt32(const std::string& value, int32_t* out) {
  int64_t parsed = 0;
  if (!ParseInt64(value, &parsed)) {
    return false;
  }
  *out = static_cast<int32_t>(parsed);
  return true;
}

std::string GetBasePrefix(const std::string& uri) {
  size_t slash = uri.rfind('/');
  if (slash == std::string::npos) {
    return uri;
  }
  return uri.substr(0, slash + 1);
}

std::string GetOrigin(const std::string& uri) {
  size_t scheme_sep = uri.find("://");
  if (scheme_sep == std::string::npos) {
    return {};
  }
  size_t host_end = uri.find('/', scheme_sep + 3);
  if (host_end == std::string::npos) {
    return uri;
  }
  return uri.substr(0, host_end);
}

}  // namespace

bool LooksLikeHlsUrl(const std::string& url) {
  const std::string lower = ToLower(url);
  return lower.find(".m3u8") != std::string::npos ||
         lower.find(".m3u") != std::string::npos;
}

std::string ResolveUri(const std::string& base_uri, const std::string& ref_uri) {
  if (ref_uri.empty()) {
    return base_uri;
  }
  if (!GetScheme(ref_uri).empty()) {
    return ref_uri;
  }
  if (ref_uri.rfind("//", 0) == 0) {
    const std::string scheme = GetScheme(base_uri);
    return scheme.empty() ? ref_uri : scheme + ":" + ref_uri;
  }
  if (!ref_uri.empty() && ref_uri.front() == '/') {
    const std::string origin = GetOrigin(base_uri);
    return origin.empty() ? ref_uri : origin + ref_uri;
  }
  return GetBasePrefix(base_uri) + ref_uri;
}

status_t ParsePlaylist(const std::string& base_uri,
                       const std::string& playlist_text,
                       Playlist& playlist) {
  playlist = Playlist();

  std::istringstream stream(playlist_text);
  std::string raw_line;
  bool saw_header = false;
  bool pending_variant = false;
  VariantPlaylistItem variant;
  int64_t pending_segment_duration_us = -1;
  bool pending_discontinuity = false;

  while (std::getline(stream, raw_line)) {
    std::string line = Trim(raw_line);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
      line = Trim(line);
    }
    if (line.empty()) {
      continue;
    }
    if (!saw_header) {
      if (line != "#EXTM3U") {
        return BAD_VALUE;
      }
      saw_header = true;
      continue;
    }

    if (line.rfind("#EXT-X-STREAM-INF:", 0) == 0) {
      playlist.is_master = true;
      variant = VariantPlaylistItem();
      auto attrs = ParseAttributes(line.substr(18));
      auto it = attrs.find("bandwidth");
      if (it != attrs.end()) {
        ParseInt64(it->second, &variant.bandwidth_bps);
      }
      pending_variant = true;
      continue;
    }

    if (line.rfind("#EXTINF:", 0) == 0) {
      size_t colon = line.find(':');
      size_t comma = line.find(',', colon == std::string::npos ? 0 : colon + 1);
      const std::string duration_text =
          line.substr(colon + 1,
                      comma == std::string::npos ? std::string::npos
                                                 : comma - colon - 1);
      if (!ParseDurationUs(duration_text, &pending_segment_duration_us)) {
        return BAD_VALUE;
      }
      continue;
    }

    if (line.rfind("#EXT-X-TARGETDURATION:", 0) == 0) {
      int64_t duration_seconds = -1;
      if (ParseInt64(line.substr(22), &duration_seconds)) {
        playlist.target_duration_us = duration_seconds * 1000000LL;
      }
      continue;
    }

    if (line.rfind("#EXT-X-MEDIA-SEQUENCE:", 0) == 0) {
      ParseInt32(line.substr(22), &playlist.media_sequence);
      continue;
    }

    if (line == "#EXT-X-ENDLIST") {
      playlist.end_list = true;
      playlist.is_live = false;
      continue;
    }

    if (line.rfind("#EXT-X-PLAYLIST-TYPE:", 0) == 0) {
      const std::string type = ToLower(Trim(line.substr(22)));
      if (type == "event") {
        playlist.is_event = true;
        playlist.is_live = true;
      } else if (type == "vod") {
        playlist.is_live = false;
      }
      continue;
    }

    if (line == "#EXT-X-DISCONTINUITY") {
      pending_discontinuity = true;
      continue;
    }

    if (line.rfind("#EXT-X-KEY:", 0) == 0) {
      auto attrs = ParseAttributes(line.substr(11));
      auto it = attrs.find("method");
      if (it != attrs.end() && ToLower(it->second) != "none") {
        playlist.is_encrypted = true;
      }
      continue;
    }

    if (line.rfind("#EXT-X-MAP:", 0) == 0) {
      playlist.has_init_segment = true;
      continue;
    }

    if (!line.empty() && line[0] == '#') {
      continue;
    }

    if (pending_variant) {
      variant.uri = ResolveUri(base_uri, line);
      playlist.variants.push_back(variant);
      pending_variant = false;
      continue;
    }

    MediaPlaylistSegment segment;
    segment.uri = ResolveUri(base_uri, line);
    segment.duration_us = pending_segment_duration_us > 0 ? pending_segment_duration_us : 0;
    segment.discontinuity = pending_discontinuity;
    segment.sequence =
        playlist.media_sequence + static_cast<int32_t>(playlist.segments.size());
    playlist.segments.push_back(segment);
    pending_segment_duration_us = -1;
    pending_discontinuity = false;
  }

  if (!saw_header) {
    return BAD_VALUE;
  }
  if (pending_variant) {
    return BAD_VALUE;
  }

  if (playlist.is_master) {
    if (playlist.variants.empty()) {
      return BAD_VALUE;
    }
    playlist.is_live = false;
    return OK;
  }

  int64_t total_duration_us = 0;
  for (const auto& segment : playlist.segments) {
    total_duration_us += segment.duration_us;
  }
  playlist.duration_us = total_duration_us;
  if (!playlist.end_list) {
    playlist.is_live = true;
  }
  return OK;
}

const VariantPlaylistItem* SelectPrimaryVariant(const Playlist& playlist) {
  if (playlist.variants.empty()) {
    return nullptr;
  }
  const VariantPlaylistItem* best = &playlist.variants.front();
  for (const auto& variant : playlist.variants) {
    if (variant.bandwidth_bps > best->bandwidth_bps) {
      best = &variant;
    }
  }
  return best;
}

}  // namespace http_live
}  // namespace player
}  // namespace ave
