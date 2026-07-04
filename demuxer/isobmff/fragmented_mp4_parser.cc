/*
 * fragmented_mp4_parser.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "demuxer/isobmff/fragmented_mp4_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

#include "demuxer/isobmff/box_types.h"
#include "media/audio/channel_layout.h"
#include "media/codec/codec_id.h"
#include "media/foundation/esds.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_mimes.h"

namespace ave::isobmff {
namespace {

using media::ERROR_MALFORMED;
using media::ERROR_UNSUPPORTED;

struct Box {
  size_t offset = 0;
  size_t size = 0;
  size_t header_size = 0;
  uint32_t type = 0;
  size_t data_offset() const { return offset + header_size; }
  size_t data_size() const { return size - header_size; }
};

bool ReadU16(const std::vector<uint8_t>& bytes,
             size_t offset,
             uint16_t* value) {
  if (!value || offset > bytes.size() || bytes.size() - offset < 2) {
    return false;
  }
  *value = (static_cast<uint16_t>(bytes[offset]) << 8) | bytes[offset + 1];
  return true;
}

bool ReadU32(const std::vector<uint8_t>& bytes,
             size_t offset,
             uint32_t* value) {
  if (!value || offset > bytes.size() || bytes.size() - offset < 4) {
    return false;
  }
  *value = (static_cast<uint32_t>(bytes[offset]) << 24) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
  return true;
}

bool ReadI32(const std::vector<uint8_t>& bytes, size_t offset, int32_t* value) {
  uint32_t unsigned_value = 0;
  if (!ReadU32(bytes, offset, &unsigned_value) || !value) {
    return false;
  }
  *value = static_cast<int32_t>(unsigned_value);
  return true;
}

bool ReadU64(const std::vector<uint8_t>& bytes,
             size_t offset,
             uint64_t* value) {
  if (!value || offset > bytes.size() || bytes.size() - offset < 8) {
    return false;
  }
  uint64_t result = 0;
  for (size_t i = 0; i < 8; ++i) {
    result = (result << 8) | bytes[offset + i];
  }
  *value = result;
  return true;
}

bool ReadBox(const std::vector<uint8_t>& bytes, size_t offset, Box* box) {
  uint32_t size32 = 0;
  uint32_t type = 0;
  if (!box || !ReadU32(bytes, offset, &size32) ||
      !ReadU32(bytes, offset + 4, &type)) {
    return false;
  }
  size_t size = size32;
  size_t header_size = 8;
  if (size32 == 1) {
    uint64_t size64 = 0;
    if (!ReadU64(bytes, offset + 8, &size64) ||
        size64 > std::numeric_limits<size_t>::max()) {
      return false;
    }
    size = static_cast<size_t>(size64);
    header_size = 16;
  } else if (size32 == 0) {
    size = bytes.size() - offset;
  }
  if (size < header_size || offset > bytes.size() ||
      size > bytes.size() - offset) {
    return false;
  }
  *box = Box{offset, size, header_size, type};
  return true;
}

template <typename Callback>
status_t ForEachChild(const std::vector<uint8_t>& bytes,
                      size_t offset,
                      size_t size,
                      Callback callback) {
  if (offset > bytes.size() || size > bytes.size() - offset) {
    return ERROR_MALFORMED;
  }
  const size_t end = offset + size;
  for (size_t cursor = offset; cursor < end;) {
    Box box;
    if (!ReadBox(bytes, cursor, &box) || box.offset + box.size > end) {
      return ERROR_MALFORMED;
    }
    const status_t err = callback(box);
    if (err != OK) {
      return err;
    }
    cursor += box.size;
  }
  return OK;
}

const Box* FindChild(const std::vector<uint8_t>& bytes,
                     size_t offset,
                     size_t size,
                     uint32_t type,
                     Box* result) {
  if (offset > bytes.size() || size > bytes.size() - offset) {
    return nullptr;
  }
  const size_t end = offset + size;
  for (size_t cursor = offset; cursor < end;) {
    Box child;
    if (!ReadBox(bytes, cursor, &child) || child.offset + child.size > end) {
      return nullptr;
    }
    if (child.type == type) {
      *result = child;
      return result;
    }
    cursor += child.size;
  }
  return nullptr;
}

std::optional<media::MediaType> MediaTypeForHandler(uint32_t handler) {
  if (handler == FOURCC_vide) {
    return media::MediaType::VIDEO;
  }
  if (handler == FOURCC_soun) {
    return media::MediaType::AUDIO;
  }
  return std::nullopt;
}

void SetChannelLayout(uint16_t channels, media::MediaMeta* format) {
  switch (channels) {
    case 1:
      format->SetChannelLayout(media::CHANNEL_LAYOUT_MONO);
      break;
    case 2:
      format->SetChannelLayout(media::CHANNEL_LAYOUT_STEREO);
      break;
    case 6:
      format->SetChannelLayout(media::CHANNEL_LAYOUT_5_1);
      break;
    case 8:
      format->SetChannelLayout(media::CHANNEL_LAYOUT_7_1);
      break;
    default:
      format->SetChannelLayout(media::CHANNEL_LAYOUT_UNSUPPORTED);
      break;
  }
}

status_t ParseSampleEntry(const std::vector<uint8_t>& bytes,
                          const Box& stsd,
                          media::MediaType media_type,
                          std::shared_ptr<media::MediaMeta>* format) {
  if (!format || stsd.data_size() < 16) {
    return ERROR_MALFORMED;
  }
  uint32_t entry_count = 0;
  if (!ReadU32(bytes, stsd.data_offset() + 4, &entry_count) ||
      entry_count == 0) {
    return ERROR_MALFORMED;
  }
  Box entry;
  if (!ReadBox(bytes, stsd.data_offset() + 8, &entry)) {
    return ERROR_MALFORMED;
  }

  const size_t payload = entry.data_offset();
  if (media_type == media::MediaType::VIDEO) {
    if (entry.data_size() < 78) {
      return ERROR_MALFORMED;
    }
    uint16_t width = 0;
    uint16_t height = 0;
    if (!ReadU16(bytes, payload + 24, &width) ||
        !ReadU16(bytes, payload + 26, &height)) {
      return ERROR_MALFORMED;
    }
    const char* mime = nullptr;
    media::CodecId codec = media::CodecId::AVE_CODEC_ID_NONE;
    if (entry.type == FOURCC_avc1 || entry.type == FOURCC_avc3) {
      mime = media::MEDIA_MIMETYPE_VIDEO_AVC;
      codec = media::CodecId::AVE_CODEC_ID_H264;
    } else if (entry.type == FOURCC_hvc1 || entry.type == FOURCC_hev1) {
      mime = media::MEDIA_MIMETYPE_VIDEO_HEVC;
      codec = media::CodecId::AVE_CODEC_ID_HEVC;
    } else {
      return ERROR_UNSUPPORTED;
    }
    auto parsed = media::MediaMeta::CreatePtr(
        media_type, media::MediaMeta::FormatType::kTrack);
    parsed->SetMime(mime).SetCodec(codec).SetWidth(width).SetHeight(height);
    Box config;
    const uint32_t config_type =
        codec == media::CodecId::AVE_CODEC_ID_H264 ? FOURCC_avcC : FOURCC_hvcC;
    if (!FindChild(bytes, payload + 78, entry.data_size() - 78, config_type,
                   &config) ||
        config.data_size() == 0) {
      return ERROR_MALFORMED;
    }
    parsed->SetPrivateData(static_cast<uint32_t>(config.data_size()),
                           bytes.data() + config.data_offset());
    *format = std::move(parsed);
    return OK;
  }

  if (media_type != media::MediaType::AUDIO || entry.type != FOURCC_mp4a ||
      entry.data_size() < 28) {
    return ERROR_UNSUPPORTED;
  }
  uint16_t channels = 0;
  uint16_t sample_size = 0;
  uint32_t sample_rate_fixed = 0;
  if (!ReadU16(bytes, payload + 16, &channels) ||
      !ReadU16(bytes, payload + 18, &sample_size) ||
      !ReadU32(bytes, payload + 24, &sample_rate_fixed)) {
    return ERROR_MALFORMED;
  }
  Box esds;
  if (!FindChild(bytes, payload + 28, entry.data_size() - 28, FOURCC_esds,
                 &esds) ||
      esds.data_size() <= 4) {
    return ERROR_MALFORMED;
  }
  media::ESDS descriptor(bytes.data() + esds.data_offset() + 4,
                         esds.data_size() - 4);
  if (descriptor.InitCheck() != OK) {
    return ERROR_MALFORMED;
  }
  const void* codec_data = nullptr;
  size_t codec_data_size = 0;
  if (descriptor.getCodecSpecificInfo(&codec_data, &codec_data_size) != OK ||
      codec_data_size == 0) {
    return ERROR_MALFORMED;
  }
  auto parsed = media::MediaMeta::CreatePtr(
      media_type, media::MediaMeta::FormatType::kTrack);
  parsed->SetMime(media::MEDIA_MIMETYPE_AUDIO_AAC)
      .SetCodec(media::CodecId::AVE_CODEC_ID_AAC)
      .SetSampleRate(sample_rate_fixed >> 16)
      .SetBitsPerSample(static_cast<int16_t>(sample_size))
      .SetPrivateData(static_cast<uint32_t>(codec_data_size), codec_data);
  SetChannelLayout(channels, parsed.get());
  *format = std::move(parsed);
  return OK;
}

status_t ParseTrack(const std::vector<uint8_t>& bytes,
                    const Box& trak,
                    FragmentedMp4Track* track) {
  Box tkhd;
  Box mdia;
  if (!track ||
      !FindChild(bytes, trak.data_offset(), trak.data_size(), FOURCC_tkhd,
                 &tkhd) ||
      !FindChild(bytes, trak.data_offset(), trak.data_size(), FOURCC_mdia,
                 &mdia) ||
      tkhd.data_size() < 16) {
    return ERROR_MALFORMED;
  }
  const uint8_t tkhd_version = bytes[tkhd.data_offset()];
  const size_t track_id_offset =
      tkhd.data_offset() + (tkhd_version == 1 ? 20 : 12);
  if (!ReadU32(bytes, track_id_offset, &track->id) || track->id == 0) {
    return ERROR_MALFORMED;
  }
  Box mdhd;
  Box hdlr;
  Box minf;
  Box stbl;
  Box stsd;
  if (!FindChild(bytes, mdia.data_offset(), mdia.data_size(), FOURCC_mdhd,
                 &mdhd) ||
      !FindChild(bytes, mdia.data_offset(), mdia.data_size(), FOURCC_hdlr,
                 &hdlr) ||
      !FindChild(bytes, mdia.data_offset(), mdia.data_size(), FOURCC_minf,
                 &minf) ||
      !FindChild(bytes, minf.data_offset(), minf.data_size(), FOURCC_stbl,
                 &stbl) ||
      !FindChild(bytes, stbl.data_offset(), stbl.data_size(), FOURCC_stsd,
                 &stsd) ||
      mdhd.data_size() < 16 || hdlr.data_size() < 12) {
    return ERROR_MALFORMED;
  }
  const uint8_t mdhd_version = bytes[mdhd.data_offset()];
  const size_t timescale_offset =
      mdhd.data_offset() + (mdhd_version == 1 ? 20 : 12);
  uint32_t handler = 0;
  if (!ReadU32(bytes, timescale_offset, &track->timescale) ||
      track->timescale == 0 ||
      !ReadU32(bytes, hdlr.data_offset() + 8, &handler)) {
    return ERROR_MALFORMED;
  }
  const auto media_type = MediaTypeForHandler(handler);
  return media_type ? ParseSampleEntry(bytes, stsd, *media_type, &track->format)
                    : ERROR_UNSUPPORTED;
}

int64_t ToMicroseconds(uint64_t time, uint32_t timescale) {
  if (timescale == 0 ||
      time > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() /
                                   1000000)) {
    return 0;
  }
  return static_cast<int64_t>(time * 1000000 / timescale);
}

struct FragmentDefaults {
  uint32_t track_id = 0;
  uint32_t duration = 0;
  uint32_t size = 0;
  uint64_t decode_time = 0;
  int64_t data_base_offset = -1;
  int64_t data_cursor = -1;
};

const FragmentedMp4Track* FindTrack(
    const std::vector<FragmentedMp4Track>& tracks,
    uint32_t id) {
  const auto iterator = std::find_if(
      tracks.begin(), tracks.end(),
      [id](const FragmentedMp4Track& track) { return track.id == id; });
  return iterator == tracks.end() ? nullptr : &*iterator;
}

status_t ParseTrun(const std::vector<uint8_t>& bytes,
                   const Box& trun,
                   const FragmentedMp4Track& track,
                   FragmentDefaults* defaults,
                   std::vector<std::shared_ptr<media::MediaFrame>>* samples) {
  if (!defaults || !samples || trun.data_size() < 8) {
    return ERROR_MALFORMED;
  }
  const size_t payload = trun.data_offset();
  const uint8_t version = bytes[payload];
  const uint32_t flags = (static_cast<uint32_t>(bytes[payload + 1]) << 16) |
                         (static_cast<uint32_t>(bytes[payload + 2]) << 8) |
                         bytes[payload + 3];
  uint32_t count = 0;
  if (!ReadU32(bytes, payload + 4, &count)) {
    return ERROR_MALFORMED;
  }
  size_t cursor = payload + 8;
  if ((flags & 0x000001U) != 0) {
    int32_t data_offset = 0;
    if (!ReadI32(bytes, cursor, &data_offset)) {
      return ERROR_MALFORMED;
    }
    defaults->data_cursor = defaults->data_base_offset + data_offset;
    cursor += 4;
  }
  if ((flags & 0x000004U) != 0) {
    cursor += 4;
  }
  const uint32_t per_sample_bytes =
      ((flags & 0x000100U) ? 4U : 0U) + ((flags & 0x000200U) ? 4U : 0U) +
      ((flags & 0x000400U) ? 4U : 0U) + ((flags & 0x000800U) ? 4U : 0U);
  if (cursor > bytes.size() ||
      (per_sample_bytes != 0 &&
       count > (bytes.size() - cursor) / per_sample_bytes)) {
    return ERROR_MALFORMED;
  }
  for (uint32_t index = 0; index < count; ++index) {
    uint32_t duration = defaults->duration;
    uint32_t size = defaults->size;
    int32_t composition_offset = 0;
    if ((flags & 0x000100U) && !ReadU32(bytes, cursor, &duration))
      return ERROR_MALFORMED;
    cursor += (flags & 0x000100U) ? 4 : 0;
    if ((flags & 0x000200U) && !ReadU32(bytes, cursor, &size))
      return ERROR_MALFORMED;
    cursor += (flags & 0x000200U) ? 4 : 0;
    cursor += (flags & 0x000400U) ? 4 : 0;
    if ((flags & 0x000800U) != 0) {
      if (version == 1) {
        if (!ReadI32(bytes, cursor, &composition_offset)) {
          return ERROR_MALFORMED;
        }
      } else {
        uint32_t unsigned_offset = 0;
        if (!ReadU32(bytes, cursor, &unsigned_offset) ||
            unsigned_offset > std::numeric_limits<int32_t>::max()) {
          return ERROR_MALFORMED;
        }
        composition_offset = static_cast<int32_t>(unsigned_offset);
      }
    }
    cursor += (flags & 0x000800U) ? 4 : 0;
    if (size == 0 || defaults->data_cursor < 0 ||
        static_cast<uint64_t>(defaults->data_cursor) > bytes.size() ||
        size > bytes.size() - static_cast<size_t>(defaults->data_cursor)) {
      return ERROR_MALFORMED;
    }
    auto frame =
        media::MediaFrame::CreateShared(size, track.format->stream_type());
    std::memcpy(frame->base(), bytes.data() + defaults->data_cursor, size);
    frame->setRange(0, size);
    const uint64_t dts = defaults->decode_time;
    const int64_t pts_ticks =
        composition_offset < 0 &&
                dts < static_cast<uint64_t>(-composition_offset)
            ? 0
            : static_cast<int64_t>(dts) + composition_offset;
    frame->SetDts(
        base::Timestamp::Micros(ToMicroseconds(dts, track.timescale)));
    frame->SetPts(base::Timestamp::Micros(
        ToMicroseconds(static_cast<uint64_t>(std::max<int64_t>(0, pts_ticks)),
                       track.timescale)));
    samples->push_back(std::move(frame));
    defaults->data_cursor += size;
    defaults->decode_time += duration;
  }
  return OK;
}

}  // namespace

status_t FragmentedMp4Parser::ParseInitSegment(
    const std::vector<uint8_t>& init_segment,
    std::vector<FragmentedMp4Track>* tracks) {
  if (!tracks)
    return BAD_VALUE;
  tracks->clear();
  Box moov;
  if (!FindChild(init_segment, 0, init_segment.size(), FOURCC_moov, &moov))
    return ERROR_MALFORMED;
  status_t result = ForEachChild(
      init_segment, moov.data_offset(), moov.data_size(), [&](const Box& box) {
        if (box.type != FOURCC_trak)
          return static_cast<status_t>(OK);
        FragmentedMp4Track track;
        const status_t err = ParseTrack(init_segment, box, &track);
        if (err == ERROR_UNSUPPORTED)
          return static_cast<status_t>(OK);
        if (err != OK)
          return err;
        tracks->push_back(std::move(track));
        return static_cast<status_t>(OK);
      });
  return result == OK && !tracks->empty() ? static_cast<status_t>(OK)
                                          : ERROR_UNSUPPORTED;
}

status_t FragmentedMp4Parser::ParseMediaSegment(
    const std::vector<uint8_t>& media_segment,
    const std::vector<FragmentedMp4Track>& tracks,
    std::vector<std::shared_ptr<media::MediaFrame>>* samples) {
  if (!samples || tracks.empty())
    return BAD_VALUE;
  samples->clear();
  Box moof;
  if (!FindChild(media_segment, 0, media_segment.size(), FOURCC_moof, &moof))
    return ERROR_MALFORMED;
  return ForEachChild(
      media_segment, moof.data_offset(), moof.data_size(),
      [&](const Box& traf) -> status_t {
        if (traf.type != FOURCC_traf)
          return static_cast<status_t>(OK);
        Box tfhd;
        if (!FindChild(media_segment, traf.data_offset(), traf.data_size(),
                       FOURCC_tfhd, &tfhd) ||
            tfhd.data_size() < 8)
          return static_cast<status_t>(ERROR_MALFORMED);
        const size_t tfhd_payload = tfhd.data_offset();
        const uint32_t tfhd_flags =
            (static_cast<uint32_t>(media_segment[tfhd_payload + 1]) << 16) |
            (static_cast<uint32_t>(media_segment[tfhd_payload + 2]) << 8) |
            media_segment[tfhd_payload + 3];
        FragmentDefaults defaults;
        if (!ReadU32(media_segment, tfhd_payload + 4, &defaults.track_id))
          return ERROR_MALFORMED;
        const FragmentedMp4Track* track = FindTrack(tracks, defaults.track_id);
        if (!track)
          return ERROR_UNSUPPORTED;
        size_t cursor = tfhd_payload + 8;
        if (tfhd_flags & 0x000001U) {
          uint64_t base_data_offset = 0;
          if (!ReadU64(media_segment, cursor, &base_data_offset) ||
              base_data_offset > std::numeric_limits<int64_t>::max())
            return ERROR_MALFORMED;
          defaults.data_base_offset = static_cast<int64_t>(base_data_offset);
          cursor += 8;
        } else {
          defaults.data_base_offset = static_cast<int64_t>(moof.offset);
        }
        defaults.data_cursor = defaults.data_base_offset;
        if (tfhd_flags & 0x000002U)
          cursor += 4;
        if ((tfhd_flags & 0x000008U) &&
            !ReadU32(media_segment, cursor, &defaults.duration))
          return ERROR_MALFORMED;
        cursor += (tfhd_flags & 0x000008U) ? 4 : 0;
        if ((tfhd_flags & 0x000010U) &&
            !ReadU32(media_segment, cursor, &defaults.size))
          return ERROR_MALFORMED;
        Box tfdt;
        if (FindChild(media_segment, traf.data_offset(), traf.data_size(),
                      FOURCC_tfdt, &tfdt)) {
          if (tfdt.data_size() < 8)
            return ERROR_MALFORMED;
          if (media_segment[tfdt.data_offset()] == 1) {
            if (!ReadU64(media_segment, tfdt.data_offset() + 4,
                         &defaults.decode_time))
              return ERROR_MALFORMED;
          } else {
            uint32_t decode_time = 0;
            if (!ReadU32(media_segment, tfdt.data_offset() + 4, &decode_time))
              return ERROR_MALFORMED;
            defaults.decode_time = decode_time;
          }
        }
        const size_t first_sample_index = samples->size();
        const status_t err =
            ForEachChild(media_segment, traf.data_offset(), traf.data_size(),
                         [&](const Box& child) {
                           return child.type == FOURCC_trun
                                      ? ParseTrun(media_segment, child, *track,
                                                  &defaults, samples)
                                      : static_cast<status_t>(OK);
                         });
        if (err != OK || first_sample_index == samples->size()) {
          return err;
        }

        // tfdt values are decode-timeline values and normally continue across
        // HLS media segments. HttpLiveSource supplies the playlist timeline, so
        // make each fragment local before it applies its segment start offset.
        const int64_t first_dts_us = (*samples)[first_sample_index]->dts().us();
        for (size_t index = first_sample_index; index < samples->size();
             ++index) {
          auto& sample = (*samples)[index];
          sample->SetDts(
              base::Timestamp::Micros(sample->dts().us() - first_dts_us));
          sample->SetPts(
              base::Timestamp::Micros(sample->pts().us() - first_dts_us));
        }
        return static_cast<status_t>(OK);
      });
}

}  // namespace ave::isobmff
