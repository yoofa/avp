/*
 * fragmented_mp4_parser.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_ISOBMFF_FRAGMENTED_MP4_PARSER_H_
#define DEMUXER_ISOBMFF_FRAGMENTED_MP4_PARSER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/errors.h"
#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"

namespace ave::isobmff {

// Immutable track information extracted from an HLS EXT-X-MAP init segment.
struct FragmentedMp4Track {
  uint32_t id = 0;
  uint32_t timescale = 0;
  std::shared_ptr<media::MediaMeta> format;
};

// Parses CMAF/fMP4 init and media segments without creating a Demuxer. The
// caller owns segment fetching and queues the resulting access units.
class FragmentedMp4Parser {
 public:
  static status_t ParseInitSegment(const std::vector<uint8_t>& init_segment,
                                   std::vector<FragmentedMp4Track>* tracks);
  static status_t ParseMediaSegment(
      const std::vector<uint8_t>& media_segment,
      const std::vector<FragmentedMp4Track>& tracks,
      std::vector<std::shared_ptr<media::MediaFrame>>* samples);
};

}  // namespace ave::isobmff

#endif  // DEMUXER_ISOBMFF_FRAGMENTED_MP4_PARSER_H_
