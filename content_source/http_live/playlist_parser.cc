/*
 * playlist_parser.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "content_source/http_live/playlist_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string GetScheme(const std::string& uri) {
  size_t colon = uri.find(':');
  if (colon == std::string::npos) {
    return {};
  }
  for (size_t i = 0; i < colon; ++i) {
    auto c = static_cast<unsigned char>(uri[i]);
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

bool ParseYesNo(const std::string& value) {
  return ToLower(Trim(value)) == "yes";
}

bool ParseByteRange(const std::string& value,
                    int64_t* length,
                    int64_t* offset) {
  if (!length || !offset) {
    return false;
  }
  *length = -1;
  *offset = -1;

  const size_t at = value.find('@');
  if (at == std::string::npos) {
    return ParseInt64(value, length);
  }

  return ParseInt64(value.substr(0, at), length) &&
         ParseInt64(value.substr(at + 1), offset);
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

std::string ResolveUri(const std::string& base_uri,
                       const std::string& ref_uri) {
  if (ref_uri.empty()) {
    return base_uri;
  }
  if (!GetScheme(ref_uri).empty()) {
    return ref_uri;
  }
  if (ref_uri.starts_with("//")) {
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
  int64_t pending_byterange_length = -1;
  int64_t pending_byterange_offset = -1;
  int64_t next_byterange_offset = 0;
  std::string current_byterange_resource;

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

    if (line.starts_with("#EXT-X-STREAM-INF:")) {
      playlist.is_master = true;
      variant = VariantPlaylistItem();
      auto attrs = ParseAttributes(line.substr(18));
      auto it = attrs.find("bandwidth");
      if (it != attrs.end()) {
        ParseInt64(it->second, &variant.bandwidth_bps);
      }
      it = attrs.find("average-bandwidth");
      if (it != attrs.end()) {
        ParseInt64(it->second, &variant.average_bandwidth_bps);
      }
      it = attrs.find("codecs");
      if (it != attrs.end()) {
        variant.codecs = it->second;
      }
      it = attrs.find("audio");
      if (it != attrs.end()) {
        variant.audio_group_id = it->second;
      }
      pending_variant = true;
      continue;
    }

    if (line.starts_with("#EXT-X-MEDIA:")) {
      auto attrs = ParseAttributes(line.substr(13));
      auto type_it = attrs.find("type");
      auto group_it = attrs.find("group-id");
      auto uri_it = attrs.find("uri");
      if (type_it != attrs.end() && ToLower(type_it->second) == "audio" &&
          group_it != attrs.end() && uri_it != attrs.end()) {
        MediaRendition rendition;
        rendition.group_id = group_it->second;
        rendition.uri = ResolveUri(base_uri, uri_it->second);
        auto default_it = attrs.find("default");
        if (default_it != attrs.end()) {
          rendition.is_default = ParseYesNo(default_it->second);
        }
        auto autoselect_it = attrs.find("autoselect");
        if (autoselect_it != attrs.end()) {
          rendition.autoselect = ParseYesNo(autoselect_it->second);
        }
        playlist.audio_renditions.push_back(std::move(rendition));
      }
      continue;
    }

    if (line.starts_with("#EXTINF:")) {
      size_t colon = line.find(':');
      size_t comma = line.find(',', colon == std::string::npos ? 0 : colon + 1);
      const std::string duration_text = line.substr(
          colon + 1,
          comma == std::string::npos ? std::string::npos : comma - colon - 1);
      if (!ParseDurationUs(duration_text, &pending_segment_duration_us)) {
        return BAD_VALUE;
      }
      continue;
    }

    if (line.starts_with("#EXT-X-TARGETDURATION:")) {
      int64_t duration_seconds = -1;
      if (ParseInt64(line.substr(22), &duration_seconds)) {
        playlist.target_duration_us = duration_seconds * 1000000LL;
      }
      continue;
    }

    if (line.starts_with("#EXT-X-MEDIA-SEQUENCE:")) {
      ParseInt32(line.substr(22), &playlist.media_sequence);
      continue;
    }

    if (line == "#EXT-X-ENDLIST") {
      playlist.end_list = true;
      playlist.is_live = false;
      continue;
    }

    if (line.starts_with("#EXT-X-PLAYLIST-TYPE:")) {
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

    if (line.starts_with("#EXT-X-KEY:")) {
      auto attrs = ParseAttributes(line.substr(11));
      auto it = attrs.find("method");
      if (it != attrs.end() && ToLower(it->second) != "none") {
        playlist.is_encrypted = true;
      }
      continue;
    }

    if (line.starts_with("#EXT-X-MAP:")) {
      playlist.has_init_segment = true;
      auto attrs = ParseAttributes(line.substr(11));
      auto uri_it = attrs.find("uri");
      if (uri_it != attrs.end()) {
        playlist.init_segment.uri = ResolveUri(base_uri, uri_it->second);
      }
      auto byterange_it = attrs.find("byterange");
      if (byterange_it != attrs.end()) {
        ParseByteRange(byterange_it->second,
                       &playlist.init_segment.byterange_length,
                       &playlist.init_segment.byterange_offset);
        if (playlist.init_segment.byterange_length > 0) {
          if (playlist.init_segment.byterange_offset < 0) {
            playlist.init_segment.byterange_offset = next_byterange_offset;
          }
          current_byterange_resource = playlist.init_segment.uri;
          next_byterange_offset = playlist.init_segment.byterange_offset +
                                  playlist.init_segment.byterange_length;
        }
      }
      continue;
    }

    if (line.starts_with("#EXT-X-BYTERANGE:")) {
      ParseByteRange(line.substr(17), &pending_byterange_length,
                     &pending_byterange_offset);
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
    segment.duration_us =
        pending_segment_duration_us > 0 ? pending_segment_duration_us : 0;
    segment.discontinuity = pending_discontinuity;
    segment.sequence = playlist.media_sequence +
                       static_cast<int32_t>(playlist.segments.size());
    if (pending_byterange_length > 0) {
      segment.byterange_length = pending_byterange_length;
      if (pending_byterange_offset >= 0) {
        segment.byterange_offset = pending_byterange_offset;
      } else if (segment.uri != current_byterange_resource) {
        segment.byterange_offset = 0;
      } else {
        segment.byterange_offset = next_byterange_offset;
      }
      current_byterange_resource = segment.uri;
      next_byterange_offset =
          segment.byterange_offset + segment.byterange_length;
    }
    playlist.segments.push_back(segment);
    pending_segment_duration_us = -1;
    pending_discontinuity = false;
    pending_byterange_length = -1;
    pending_byterange_offset = -1;
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

  auto score_variant = [](const VariantPlaylistItem& variant) {
    const std::string codecs = ToLower(variant.codecs);
    int score = 0;
    if (codecs.find("avc") != std::string::npos ||
        codecs.find("hvc") != std::string::npos ||
        codecs.find("hev") != std::string::npos ||
        codecs.find("vp0") != std::string::npos ||
        codecs.find("av01") != std::string::npos) {
      // An audio-only rendition is not a suitable primary stream when a
      // video variant is available.
      score += 1000;
    }
    if (codecs.find("mp4a.") != std::string::npos) {
      score += 100;
    }
    if (codecs.find("ac-3") != std::string::npos ||
        codecs.find("ec-3") != std::string::npos) {
      score -= 100;
    }
    return score;
  };

  const VariantPlaylistItem* best = &playlist.variants.front();
  for (const auto& variant : playlist.variants) {
    const int variant_score = score_variant(variant);
    const int best_score = score_variant(*best);
    // There is no adaptive bandwidth estimator yet. After preferring a video
    // rendition, keep the original highest-bandwidth policy so a low-frame-
    // rate fallback rendition is not mistaken for playback stutter.
    if (variant_score > best_score ||
        (variant_score == best_score &&
         variant.bandwidth_bps > best->bandwidth_bps)) {
      best = &variant;
    }
  }
  return best;
}

const MediaRendition* SelectAudioRendition(const Playlist& playlist,
                                           const std::string& group_id) {
  if (group_id.empty()) {
    return nullptr;
  }

  const MediaRendition* selected = nullptr;
  for (const auto& rendition : playlist.audio_renditions) {
    if (rendition.group_id != group_id) {
      continue;
    }
    if (!selected) {
      selected = &rendition;
    }
    if (rendition.is_default) {
      return &rendition;
    }
  }
  return selected;
}

}  // namespace http_live
}  // namespace player
}  // namespace ave
