/*
 * mp4_demuxer.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "mp4_demuxer.h"

#include <algorithm>
#include <cstring>

#include "base/logging.h"
#include "base/units/time_delta.h"
#include "base/units/timestamp.h"
#include "demuxer/isobmff/box_reader.h"
#include "demuxer/isobmff/box_types.h"
#include "media/audio/channel_layout.h"
#include "media/codec/codec_id.h"
#include "media/foundation/aac_utils.h"
#include "media/foundation/avc_utils.h"
#include "media/foundation/esds.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_mimes.h"

namespace ave {
namespace player {

using namespace isobmff;
using namespace media;

static constexpr int kMaxBoxDepth = 64;
static constexpr off64_t kMaxBoxSize = 256LL * 1024 * 1024;  // 256 MB

// ========== Mp4Source ==========

Mp4Source::Mp4Source(Mp4Demuxer* demuxer,
                     size_t track_index,
                     std::shared_ptr<MediaMeta> format)
    : demuxer_(demuxer),
      track_index_(track_index),
      format_(std::move(format)) {}

status_t Mp4Source::Start(std::shared_ptr<ave::media::Message> /*params*/) {
  started_ = true;
  return OK;
}

status_t Mp4Source::Stop() {
  started_ = false;
  return OK;
}

std::shared_ptr<MediaMeta> Mp4Source::GetFormat() {
  return format_;
}

status_t Mp4Source::Read(std::shared_ptr<MediaFrame>& frame,
                         const ReadOptions* options) {
  if (!started_) {
    return NO_INIT;
  }
  return demuxer_->ReadSample(track_index_, frame, options);
}

// ========== Mp4Demuxer ==========

Mp4Demuxer::Mp4Demuxer(std::shared_ptr<ave::DataSource> data_source)
    : Demuxer(std::move(data_source)) {
  AVE_LOG(LS_INFO) << "Mp4Demuxer created";
}

Mp4Demuxer::~Mp4Demuxer() {
  AVE_LOG(LS_INFO) << "Mp4Demuxer destroyed";
}

const char* Mp4Demuxer::name() {
  return "Mp4Demuxer";
}

status_t Mp4Demuxer::Init() {
  off64_t file_size = 0;
  if (data_source_->GetSize(&file_size) != OK || file_size <= 0) {
    AVE_LOG(LS_ERROR) << "Mp4Demuxer: cannot get file size";
    return ERROR_IO;
  }

  AVE_LOG(LS_INFO) << "Mp4Demuxer::Init file_size=" << file_size;

  status_t err = ParseBoxes(0, file_size, 0);
  if (err != OK) {
    AVE_LOG(LS_ERROR) << "Mp4Demuxer: failed to parse boxes: " << err;
    return err;
  }

  if (tracks_.empty()) {
    AVE_LOG(LS_ERROR) << "Mp4Demuxer: no tracks found";
    return ERROR_MALFORMED;
  }

  // Build source format
  source_format_ = MediaMeta::CreatePtr(media::MediaType::UNKNOWN,
                                        MediaMeta::FormatType::kTrack);

  // Find overall duration (max of all tracks)
  int64_t max_duration_us = 0;
  for (const auto& track : tracks_) {
    max_duration_us = std::max(max_duration_us, track.duration_us);
  }
  if (max_duration_us > 0) {
    source_format_->SetDuration(base::TimeDelta::Micros(max_duration_us));
  }

  initialized_ = true;
  AVE_LOG(LS_INFO) << "Mp4Demuxer::Init done, " << tracks_.size()
                   << " tracks, duration=" << max_duration_us / 1000 << "ms";
  return OK;
}

status_t Mp4Demuxer::GetFormat(std::shared_ptr<MediaMeta>& format) {
  if (!initialized_) {
    return NO_INIT;
  }
  format = source_format_;
  return OK;
}

size_t Mp4Demuxer::GetTrackCount() {
  return tracks_.size();
}

status_t Mp4Demuxer::GetTrackFormat(std::shared_ptr<MediaMeta>& format,
                                    size_t track_index) {
  if (track_index >= tracks_.size()) {
    return BAD_VALUE;
  }
  format = tracks_[track_index].meta;
  return OK;
}

std::shared_ptr<MediaSource> Mp4Demuxer::GetTrack(size_t track_index) {
  if (track_index >= tracks_.size()) {
    return nullptr;
  }
  return std::make_shared<Mp4Source>(this, track_index,
                                     tracks_[track_index].meta);
}

// ========== Box Parsing ==========

status_t Mp4Demuxer::ParseBoxes(off64_t offset, off64_t end_offset, int depth) {
  if (depth > kMaxBoxDepth) {
    AVE_LOG(LS_ERROR) << "Box nesting too deep: " << depth;
    return ERROR_MALFORMED;
  }

  while (offset < end_offset) {
    BoxHeader header;
    status_t err = ReadBoxHeader(data_source_.get(), offset, &header);
    if (err != OK) {
      if (offset > 0) {
        // May have reached end of file, not an error if we parsed something
        break;
      }
      return err;
    }

    if (header.size > kMaxBoxSize && header.type != FOURCC_mdat) {
      AVE_LOG(LS_WARNING) << "Skipping oversized box, size=" << header.size;
      offset += header.size;
      continue;
    }

    char fourcc[5];
    FourCCToString(header.type, fourcc);
    AVE_LOG(LS_VERBOSE) << "Box '" << fourcc << "' offset=" << header.offset
                        << " size=" << header.size << " depth=" << depth;

    switch (header.type) {
      case FOURCC_moov:
        err = ParseMoov(header.data_offset(), header.data_size());
        if (err != OK) {
          return err;
        }
        break;

      case FOURCC_mdat:
      case FOURCC_free:
      case FOURCC_skip:
      case FOURCC_wide:
        // Skip data boxes
        break;

      case FOURCC_ftyp:
        // Could validate compatible brands; skip for now.
        break;

      default:
        break;
    }

    off64_t next = header.offset + header.size;
    if (next <= offset) {
      break;  // prevent infinite loop
    }
    offset = next;
  }

  return OK;
}

status_t Mp4Demuxer::ParseMoov(off64_t offset, off64_t size) {
  AVE_LOG(LS_INFO) << "ParseMoov offset=" << offset << " size=" << size;

  off64_t end = offset + size;
  off64_t pos = offset;

  while (pos < end) {
    BoxHeader header;
    status_t err = ReadBoxHeader(data_source_.get(), pos, &header);
    if (err != OK) {
      break;
    }

    switch (header.type) {
      case FOURCC_trak:
        err = ParseTrak(header.data_offset(), header.data_size());
        if (err != OK) {
          AVE_LOG(LS_WARNING) << "Failed to parse trak, skipping";
        }
        break;

      case FOURCC_mvhd:
        // Could parse movie header for timescale; not strictly needed.
        break;

      default:
        break;
    }

    off64_t next = header.offset + header.size;
    if (next <= pos) {
      break;
    }
    pos = next;
  }

  return OK;
}

status_t Mp4Demuxer::ParseTrak(off64_t offset, off64_t size) {
  AVE_LOG(LS_INFO) << "ParseTrak offset=" << offset << " size=" << size;

  TrakParseContext ctx;
  ctx.track.sample_table = std::make_unique<SampleTable>(data_source_.get());

  off64_t end = offset + size;
  off64_t pos = offset;

  // First pass: find mdia and parse it
  while (pos < end) {
    BoxHeader header;
    status_t err = ReadBoxHeader(data_source_.get(), pos, &header);
    if (err != OK) {
      break;
    }

    if (header.type == FOURCC_mdia) {
      err = ParseMdiaBox(header.data_offset(), header.data_size(), &ctx);
      if (err != OK) {
        return err;
      }
    }

    off64_t next = header.offset + header.size;
    if (next <= pos)
      break;
    pos = next;
  }

  if (!ctx.has_mdhd || !ctx.has_hdlr || !ctx.has_stbl) {
    AVE_LOG(LS_WARNING) << "Incomplete trak: mdhd=" << ctx.has_mdhd
                        << " hdlr=" << ctx.has_hdlr << " stbl=" << ctx.has_stbl;
    return ERROR_MALFORMED;
  }

  if (ctx.track.media_type == media::MediaType::UNKNOWN) {
    AVE_LOG(LS_INFO) << "Skipping unknown-type track";
    return OK;
  }

  tracks_.push_back(std::move(ctx.track));
  AVE_LOG(LS_INFO) << "Added track #" << tracks_.size() - 1 << " type="
                   << (tracks_.back().media_type == media::MediaType::VIDEO
                           ? "VIDEO"
                           : "AUDIO")
                   << " samples="
                   << tracks_.back().sample_table->CountSamples();

  return OK;
}

// Parse mdia container (mdhd + hdlr + minf)
status_t Mp4Demuxer::ParseMdiaBox(off64_t offset,
                                  off64_t size,
                                  TrakParseContext* ctx) {
  off64_t end = offset + size;
  off64_t pos = offset;

  while (pos < end) {
    BoxHeader header;
    status_t err = ReadBoxHeader(data_source_.get(), pos, &header);
    if (err != OK)
      break;

    switch (header.type) {
      case FOURCC_mdhd:
        err = ParseMdhd(header.data_offset(), header.data_size(), &ctx->track);
        if (err == OK)
          ctx->has_mdhd = true;
        break;

      case FOURCC_hdlr:
        err = ParseHdlr(header.data_offset(), header.data_size(), &ctx->track);
        if (err == OK)
          ctx->has_hdlr = true;
        break;

      case FOURCC_minf: {
        // Parse minf → stbl
        off64_t minf_end = header.data_offset() + header.data_size();
        off64_t minf_pos = header.data_offset();
        while (minf_pos < minf_end) {
          BoxHeader sub;
          err = ReadBoxHeader(data_source_.get(), minf_pos, &sub);
          if (err != OK)
            break;
          if (sub.type == FOURCC_stbl) {
            err = ParseStbl(sub.data_offset(), sub.data_size(), &ctx->track);
            if (err == OK)
              ctx->has_stbl = true;
          }
          off64_t next = sub.offset + sub.size;
          if (next <= minf_pos)
            break;
          minf_pos = next;
        }
        break;
      }

      default:
        break;
    }

    off64_t next = header.offset + header.size;
    if (next <= pos)
      break;
    pos = next;
  }

  return OK;
}

status_t Mp4Demuxer::ParseMdhd(off64_t offset, off64_t size, Track* track) {
  if (size < 4) {
    return ERROR_MALFORMED;
  }

  uint8_t version = 0;
  uint32_t flags = 0;
  status_t err =
      ReadFullBoxHeader(data_source_.get(), offset, &version, &flags);
  if (err != OK) {
    return err;
  }

  off64_t pos = offset + 4;  // skip version+flags

  uint32_t timescale = 0;
  uint64_t duration = 0;

  if (version == 1) {
    // 8 bytes creation_time + 8 modification_time + 4 timescale + 8 duration
    if (size < 4 + 28) {
      return ERROR_MALFORMED;
    }
    pos += 16;  // skip creation_time + modification_time
    if (!data_source_->GetUInt32(pos, &timescale)) {
      return ERROR_IO;
    }
    pos += 4;
    if (!data_source_->GetUInt64(pos, &duration)) {
      return ERROR_IO;
    }
  } else {
    // version 0: 4+4+4+4 = creation_time + modification_time + timescale +
    // duration
    if (size < 4 + 16) {
      return ERROR_MALFORMED;
    }
    pos += 8;  // skip creation_time + modification_time
    if (!data_source_->GetUInt32(pos, &timescale)) {
      return ERROR_IO;
    }
    pos += 4;
    uint32_t dur32 = 0;
    if (!data_source_->GetUInt32(pos, &dur32)) {
      return ERROR_IO;
    }
    duration = dur32;
  }

  if (timescale == 0) {
    return ERROR_MALFORMED;
  }

  track->timescale = timescale;
  track->sample_table->SetTimescale(timescale);
  track->duration_us = static_cast<int64_t>(duration) * 1000000LL / timescale;

  AVE_LOG(LS_INFO) << "mdhd: timescale=" << timescale
                   << " duration=" << track->duration_us / 1000 << "ms";
  return OK;
}

status_t Mp4Demuxer::ParseHdlr(off64_t offset, off64_t size, Track* track) {
  if (size < 4 + 12) {
    return ERROR_MALFORMED;
  }

  off64_t pos = offset + 4;  // skip version+flags
  pos += 4;                  // skip pre_defined

  uint32_t handler_type = 0;
  if (!data_source_->GetUInt32(pos, &handler_type)) {
    return ERROR_IO;
  }

  switch (handler_type) {
    case FOURCC_vide:
      track->media_type = media::MediaType::VIDEO;
      break;
    case FOURCC_soun:
      track->media_type = media::MediaType::AUDIO;
      break;
    case FOURCC_subt:
    case FOURCC_text:
      track->media_type = media::MediaType::SUBTITLE;
      break;
    default: {
      char fourcc[5];
      FourCCToString(handler_type, fourcc);
      AVE_LOG(LS_INFO) << "hdlr: unknown handler type '" << fourcc << "'";
      track->media_type = media::MediaType::UNKNOWN;
      break;
    }
  }

  return OK;
}

status_t Mp4Demuxer::ParseStbl(off64_t offset, off64_t size, Track* track) {
  AVE_LOG(LS_INFO) << "ParseStbl offset=" << offset << " size=" << size;

  off64_t end = offset + size;
  off64_t pos = offset;

  while (pos < end) {
    BoxHeader header;
    status_t err = ReadBoxHeader(data_source_.get(), pos, &header);
    if (err != OK) {
      break;
    }

    off64_t data_offset = header.data_offset();
    off64_t data_size = header.data_size();

    // Full-box tables need version+flags skipped
    uint8_t version = 0;
    uint32_t flags = 0;

    switch (header.type) {
      case FOURCC_stsd:
        err = ParseStsd(data_offset, data_size, track);
        break;

      case FOURCC_stts:
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetTimeToSampleParams(data_offset + 4,
                                                           data_size - 4);
        }
        break;

      case FOURCC_ctts:
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetCompositionTimeToSampleParams(
              data_offset + 4, data_size - 4);
        }
        break;

      case FOURCC_stsc:
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetSampleToChunkParams(data_offset + 4,
                                                            data_size - 4);
        }
        break;

      case FOURCC_stsz:
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetSampleSizeParams(data_offset + 4,
                                                         data_size - 4);
        }
        break;

      case FOURCC_stz2:
        // Compact sample sizes — treat same as stsz for now
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetSampleSizeParams(data_offset + 4,
                                                         data_size - 4);
        }
        break;

      case FOURCC_stco:
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetChunkOffsetParams(
              data_offset + 4, data_size - 4, /*is_co64=*/false);
        }
        break;

      case FOURCC_co64:
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetChunkOffsetParams(
              data_offset + 4, data_size - 4, /*is_co64=*/true);
        }
        break;

      case FOURCC_stss:
        err = ReadFullBoxHeader(data_source_.get(), data_offset, &version,
                                &flags);
        if (err == OK) {
          err = track->sample_table->SetSyncSampleParams(data_offset + 4,
                                                         data_size - 4);
        }
        break;

      default:
        break;
    }

    if (err != OK) {
      char fourcc[5];
      FourCCToString(header.type, fourcc);
      AVE_LOG(LS_WARNING) << "Failed to parse stbl child '" << fourcc
                          << "': " << err;
    }

    off64_t next = header.offset + header.size;
    if (next <= pos)
      break;
    pos = next;
  }

  return OK;
}

status_t Mp4Demuxer::ParseStsd(off64_t offset, off64_t size, Track* track) {
  if (size < 8) {
    return ERROR_MALFORMED;
  }

  // stsd is a full box: version(1) + flags(3) + entry_count(4)
  uint8_t version = 0;
  uint32_t flags = 0;
  status_t err =
      ReadFullBoxHeader(data_source_.get(), offset, &version, &flags);
  if (err != OK) {
    return err;
  }

  uint32_t entry_count = 0;
  if (!data_source_->GetUInt32(offset + 4, &entry_count)) {
    return ERROR_IO;
  }

  if (entry_count == 0) {
    return ERROR_MALFORMED;
  }

  // Parse first sample entry
  off64_t entry_offset = offset + 8;
  off64_t remaining = size - 8;

  if (remaining < 8) {
    return ERROR_MALFORMED;
  }

  BoxHeader entry_header;
  err = ReadBoxHeader(data_source_.get(), entry_offset, &entry_header);
  if (err != OK) {
    return err;
  }

  uint32_t entry_type = entry_header.type;

  switch (entry_type) {
    case FOURCC_avc1:
    case FOURCC_avc3:
    case FOURCC_hvc1:
    case FOURCC_hev1:
    case FOURCC_mp4v:
    case FOURCC_vp09:
    case FOURCC_av01:
      return ParseVideoSampleEntry(entry_header.data_offset(),
                                   entry_header.data_size(), entry_type, track);

    case FOURCC_mp4a:
    case FOURCC_Opus:
    case FOURCC_fLaC:
    case FOURCC_ac_3:
    case FOURCC_ec_3:
    case FOURCC_alac:
    case FOURCC_samr:
    case FOURCC_sawb:
      return ParseAudioSampleEntry(entry_header.data_offset(),
                                   entry_header.data_size(), entry_type, track);

    default: {
      char fourcc[5];
      FourCCToString(entry_type, fourcc);
      AVE_LOG(LS_WARNING) << "Unknown stsd entry type: '" << fourcc << "'";
      return ERROR_UNSUPPORTED;
    }
  }
}

// ========== Video Sample Entry ==========

status_t Mp4Demuxer::ParseVideoSampleEntry(off64_t offset,
                                           off64_t size,
                                           uint32_t type,
                                           Track* track) {
  // VisualSampleEntry: 6 reserved + 2 data_ref_index + 2 pre_defined +
  // 2 reserved + 12 pre_defined + 2 width + 2 height + 4 hres + 4 vres +
  // 4 reserved + 2 frame_count + 32 compressorname + 2 depth + 2 pre_defined
  // Total fixed: 78 bytes after box header
  if (size < 78) {
    return ERROR_MALFORMED;
  }

  uint16_t width = 0;
  uint16_t height = 0;
  if (!data_source_->GetUInt16(offset + 24, &width) ||
      !data_source_->GetUInt16(offset + 26, &height)) {
    return ERROR_IO;
  }

  CodecId codec_id = CodecId::AVE_CODEC_ID_NONE;
  const char* mime = nullptr;
  switch (type) {
    case FOURCC_avc1:
    case FOURCC_avc3:
      codec_id = CodecId::AVE_CODEC_ID_H264;
      mime = MEDIA_MIMETYPE_VIDEO_AVC;
      break;
    case FOURCC_hvc1:
    case FOURCC_hev1:
      codec_id = CodecId::AVE_CODEC_ID_HEVC;
      mime = MEDIA_MIMETYPE_VIDEO_HEVC;
      break;
    case FOURCC_mp4v:
      codec_id = CodecId::AVE_CODEC_ID_MPEG4;
      mime = MEDIA_MIMETYPE_VIDEO_MPEG4;
      break;
    case FOURCC_vp09:
      codec_id = CodecId::AVE_CODEC_ID_VP9;
      mime = MEDIA_MIMETYPE_VIDEO_VP9;
      break;
    case FOURCC_av01:
      codec_id = CodecId::AVE_CODEC_ID_AV1;
      mime = MEDIA_MIMETYPE_VIDEO_AV1;
      break;
    default:
      break;
  }

  track->meta = MediaMeta::CreatePtr(media::MediaType::VIDEO,
                                     MediaMeta::FormatType::kTrack);
  track->meta->SetCodec(codec_id);
  if (mime) {
    track->meta->SetMime(mime);
  }
  track->meta->SetWidth(width);
  track->meta->SetHeight(height);
  if (track->duration_us > 0) {
    track->meta->SetDuration(base::TimeDelta::Micros(track->duration_us));
  }

  AVE_LOG(LS_INFO) << "Video entry: " << width << "x" << height
                   << " mime=" << (mime ? mime : "unknown");

  // Parse child boxes for codec-specific data (avcC, hvcC, esds, etc.)
  off64_t child_offset = offset + 78;  // past fixed visual sample entry fields
  off64_t child_end = offset + size;

  while (child_offset < child_end) {
    BoxHeader child;
    status_t err = ReadBoxHeader(data_source_.get(), child_offset, &child);
    if (err != OK) {
      break;
    }

    if (child.type == FOURCC_avcC) {
      // Read entire avcC data as private data (AVCC format)
      off64_t avcc_size = child.data_size();
      if (avcc_size > 0 && avcc_size < 1024 * 1024) {
        std::vector<uint8_t> avcc_data(avcc_size);
        if (data_source_->ReadAt(child.data_offset(), avcc_data.data(),
                                 avcc_size) == avcc_size) {
          track->meta->SetPrivateData(static_cast<uint32_t>(avcc_size),
                                      avcc_data.data());
          AVE_LOG(LS_INFO) << "avcC: " << avcc_size << " bytes";
        }
      }
    } else if (child.type == FOURCC_hvcC) {
      off64_t hvcc_size = child.data_size();
      if (hvcc_size > 0 && hvcc_size < 1024 * 1024) {
        std::vector<uint8_t> hvcc_data(hvcc_size);
        if (data_source_->ReadAt(child.data_offset(), hvcc_data.data(),
                                 hvcc_size) == hvcc_size) {
          track->meta->SetPrivateData(static_cast<uint32_t>(hvcc_size),
                                      hvcc_data.data());
          AVE_LOG(LS_INFO) << "hvcC: " << hvcc_size << " bytes";
        }
      }
    } else if (child.type == FOURCC_esds) {
      // MPEG-4 visual esds
      off64_t esds_size = child.data_size();
      if (esds_size > 4 && esds_size < 1024 * 1024) {
        std::vector<uint8_t> esds_data(esds_size);
        if (data_source_->ReadAt(child.data_offset(), esds_data.data(),
                                 esds_size) == esds_size) {
          // Skip version+flags (4 bytes) then parse ESDS
          media::ESDS esds(esds_data.data() + 4, esds_size - 4);
          if (esds.InitCheck() == OK) {
            const void* codec_data = nullptr;
            size_t codec_data_size = 0;
            if (esds.getCodecSpecificInfo(&codec_data, &codec_data_size) ==
                OK) {
              track->meta->SetPrivateData(
                  static_cast<uint32_t>(codec_data_size),
                  const_cast<void*>(codec_data));
            }
          }
        }
      }
    }

    off64_t next = child.offset + child.size;
    if (next <= child_offset)
      break;
    child_offset = next;
  }

  return OK;
}

// ========== Audio Sample Entry ==========

status_t Mp4Demuxer::ParseAudioSampleEntry(off64_t offset,
                                           off64_t size,
                                           uint32_t type,
                                           Track* track) {
  // AudioSampleEntry: 6 reserved + 2 data_ref_index + 8 reserved +
  // 2 channelcount + 2 samplesize + 2 pre_defined + 2 reserved +
  // 4 samplerate (16.16 fixed-point)
  // Total fixed: 28 bytes after box header
  if (size < 28) {
    return ERROR_MALFORMED;
  }

  uint16_t channel_count = 0;
  uint16_t sample_size_bits = 0;
  uint32_t sample_rate_fixed = 0;

  if (!data_source_->GetUInt16(offset + 16, &channel_count) ||
      !data_source_->GetUInt16(offset + 18, &sample_size_bits) ||
      !data_source_->GetUInt32(offset + 24, &sample_rate_fixed)) {
    return ERROR_IO;
  }

  uint32_t sample_rate = sample_rate_fixed >> 16;  // 16.16 fixed-point

  CodecId codec_id = CodecId::AVE_CODEC_ID_NONE;
  const char* mime = nullptr;
  switch (type) {
    case FOURCC_mp4a:
      codec_id = CodecId::AVE_CODEC_ID_AAC;
      mime = MEDIA_MIMETYPE_AUDIO_AAC;
      break;
    case FOURCC_Opus:
      codec_id = CodecId::AVE_CODEC_ID_OPUS;
      mime = MEDIA_MIMETYPE_AUDIO_OPUS;
      break;
    case FOURCC_fLaC:
      codec_id = CodecId::AVE_CODEC_ID_FLAC;
      mime = MEDIA_MIMETYPE_AUDIO_FLAC;
      break;
    case FOURCC_ac_3:
      codec_id = CodecId::AVE_CODEC_ID_AC3;
      mime = MEDIA_MIMETYPE_AUDIO_AC3;
      break;
    case FOURCC_ec_3:
      codec_id = CodecId::AVE_CODEC_ID_EAC3;
      mime = MEDIA_MIMETYPE_AUDIO_EAC3;
      break;
    case FOURCC_alac:
      codec_id = CodecId::AVE_CODEC_ID_ALAC;
      mime = MEDIA_MIMETYPE_AUDIO_ALAC;
      break;
    default:
      break;
  }

  track->meta = MediaMeta::CreatePtr(media::MediaType::AUDIO,
                                     MediaMeta::FormatType::kTrack);
  track->meta->SetCodec(codec_id);
  if (mime) {
    track->meta->SetMime(mime);
  }
  track->meta->SetSampleRate(sample_rate);
  track->meta->SetBitsPerSample(static_cast<int16_t>(sample_size_bits));
  if (track->duration_us > 0) {
    track->meta->SetDuration(base::TimeDelta::Micros(track->duration_us));
  }

  // Map channel count to ChannelLayout
  switch (channel_count) {
    case 1:
      track->meta->SetChannelLayout(media::CHANNEL_LAYOUT_MONO);
      break;
    case 2:
      track->meta->SetChannelLayout(media::CHANNEL_LAYOUT_STEREO);
      break;
    case 6:
      track->meta->SetChannelLayout(media::CHANNEL_LAYOUT_5_1);
      break;
    case 8:
      track->meta->SetChannelLayout(media::CHANNEL_LAYOUT_7_1);
      break;
    default:
      track->meta->SetChannelLayout(media::CHANNEL_LAYOUT_UNSUPPORTED);
      break;
  }

  AVE_LOG(LS_INFO) << "Audio entry: sr=" << sample_rate
                   << " ch=" << channel_count << " bits=" << sample_size_bits
                   << " mime=" << (mime ? mime : "unknown");

  // Parse child boxes for codec-specific data
  off64_t child_offset = offset + 28;
  off64_t child_end = offset + size;

  while (child_offset < child_end) {
    BoxHeader child;
    status_t err = ReadBoxHeader(data_source_.get(), child_offset, &child);
    if (err != OK) {
      break;
    }

    if (child.type == FOURCC_esds) {
      off64_t esds_size = child.data_size();
      if (esds_size > 4 && esds_size < 1024 * 1024) {
        std::vector<uint8_t> esds_data(esds_size);
        if (data_source_->ReadAt(child.data_offset(), esds_data.data(),
                                 esds_size) == esds_size) {
          media::ESDS esds(esds_data.data() + 4, esds_size - 4);
          if (esds.InitCheck() == OK) {
            // Extract codec-specific config (AudioSpecificConfig for AAC)
            const void* codec_data = nullptr;
            size_t codec_data_size = 0;
            if (esds.getCodecSpecificInfo(&codec_data, &codec_data_size) ==
                OK) {
              track->meta->SetPrivateData(
                  static_cast<uint32_t>(codec_data_size),
                  const_cast<void*>(codec_data));
            }

            // Update sample rate from esds if available (may override stsd)
            uint8_t object_type = 0;
            esds.getObjectTypeIndication(&object_type);
            // 0x40 = Audio ISO/IEC 14496-3
            if (object_type == 0x40 && codec_data_size >= 2) {
              const uint8_t* asc = static_cast<const uint8_t*>(codec_data);
              uint8_t freq_index = ((asc[0] & 0x07) << 1) | (asc[1] >> 7);
              uint32_t asc_sr = ave::media::GetSamplingRate(freq_index);
              if (asc_sr > 0) {
                track->meta->SetSampleRate(asc_sr);
                AVE_LOG(LS_INFO) << "AAC ASC sample_rate=" << asc_sr;
              }
              uint8_t ch_config = (asc[1] >> 3) & 0x0f;
              uint8_t asc_ch = ave::media::GetChannelCount(ch_config);
              if (asc_ch > 0 && asc_ch != channel_count) {
                AVE_LOG(LS_INFO) << "AAC ASC channels=" << (int)asc_ch;
              }
            }
          }
        }
      }
    } else if (child.type == FOURCC_dOps) {
      // Opus codec-specific
      off64_t dops_size = child.data_size();
      if (dops_size > 0 && dops_size < 1024 * 1024) {
        std::vector<uint8_t> dops_data(dops_size);
        if (data_source_->ReadAt(child.data_offset(), dops_data.data(),
                                 dops_size) == dops_size) {
          track->meta->SetPrivateData(static_cast<uint32_t>(dops_size),
                                      dops_data.data());
        }
      }
    } else if (child.type == FOURCC_dfLa) {
      // FLAC codec-specific
      off64_t dfla_size = child.data_size();
      if (dfla_size > 4 && dfla_size < 1024 * 1024) {
        // Skip version+flags
        std::vector<uint8_t> dfla_data(dfla_size - 4);
        if (data_source_->ReadAt(child.data_offset() + 4, dfla_data.data(),
                                 dfla_size - 4) == dfla_size - 4) {
          track->meta->SetPrivateData(static_cast<uint32_t>(dfla_size - 4),
                                      dfla_data.data());
        }
      }
    } else if (child.type == FOURCC_dac3 || child.type == FOURCC_dec3) {
      off64_t dac_size = child.data_size();
      if (dac_size > 0 && dac_size < 1024 * 1024) {
        std::vector<uint8_t> dac_data(dac_size);
        if (data_source_->ReadAt(child.data_offset(), dac_data.data(),
                                 dac_size) == dac_size) {
          track->meta->SetPrivateData(static_cast<uint32_t>(dac_size),
                                      dac_data.data());
        }
      }
    }

    off64_t next = child.offset + child.size;
    if (next <= child_offset)
      break;
    child_offset = next;
  }

  return OK;
}

// ========== Sample Reading ==========

status_t Mp4Demuxer::ReadSample(size_t track_index,
                                std::shared_ptr<MediaFrame>& frame,
                                const MediaSource::ReadOptions* options) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (track_index >= tracks_.size()) {
    return BAD_VALUE;
  }

  auto& track = tracks_[track_index];

  // Handle seek
  if (options) {
    int64_t seek_time_us = 0;
    MediaSource::ReadOptions::SeekMode mode;
    if (options->GetSeekTo(&seek_time_us, &mode)) {
      int flags = SampleTable::kFlagBefore;
      switch (mode) {
        case MediaSource::ReadOptions::SEEK_PREVIOUS_SYNC:
          flags = SampleTable::kFlagBefore;
          break;
        case MediaSource::ReadOptions::SEEK_NEXT_SYNC:
          flags = SampleTable::kFlagAfter;
          break;
        case MediaSource::ReadOptions::SEEK_CLOSEST_SYNC:
        case MediaSource::ReadOptions::SEEK_CLOSEST:
          flags = SampleTable::kFlagClosest;
          break;
      }

      uint32_t sample_index = 0;
      status_t err = track.sample_table->FindSyncSampleNear(
          seek_time_us, &sample_index, flags);
      if (err != OK) {
        return err;
      }
      track.current_sample = sample_index;
    }
  }

  if (track.current_sample >= track.sample_table->CountSamples()) {
    return ERROR_END_OF_STREAM;
  }

  SampleInfo info;
  status_t err = track.sample_table->GetSampleInfo(track.current_sample, &info);
  if (err != OK) {
    return err;
  }

  // Create MediaFrame and read sample data
  frame = MediaFrame::CreateShared(info.size, track.media_type);
  if (!frame) {
    return NO_MEMORY;
  }

  ssize_t bytes_read =
      data_source_->ReadAt(info.offset, frame->data(), info.size);
  if (bytes_read < 0 || static_cast<uint32_t>(bytes_read) != info.size) {
    return ERROR_IO;
  }
  frame->setRange(0, info.size);

  // Set timestamps
  frame->SetPts(base::Timestamp::Micros(info.pts_us));
  frame->SetDts(base::Timestamp::Micros(info.dts_us));
  frame->SetDuration(base::TimeDelta::Micros(info.duration_us));
  frame->SetCodec(track.meta->codec());
  frame->SetStreamType(track.media_type);

  track.current_sample++;

  return OK;
}

}  // namespace player
}  // namespace ave
