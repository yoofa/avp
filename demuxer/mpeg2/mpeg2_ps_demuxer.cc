/*
 * mpeg2_ps_demuxer.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "demuxer/mpeg2/mpeg2_ps_demuxer.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "base/logging.h"
#include "demuxer/mpeg2/mpeg2_common.h"
#include "media/foundation/bit_reader.h"
#include "media/foundation/buffer.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_mimes.h"
#include "media/modules/mpeg2ts/es_queue.h"
#include "media/modules/mpeg2ts/packet_source.h"
#include "media/modules/mpeg2ts/ts_parser.h"

namespace ave {
namespace player {

namespace {

constexpr size_t kChunkSize = 8192;
constexpr off64_t kMaxInitBytes = 1024 * 1024;

ssize_t FindStartCodeOffset(const uint8_t* data, size_t size) {
  for (size_t i = 0; i + 3 < size; ++i) {
    if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
      return static_cast<ssize_t>(i);
    }
  }
  return -1;
}

}  // namespace

Mpeg2PsDemuxer::Mpeg2PsDemuxer(std::shared_ptr<ave::DataSource> data_source)
    : Demuxer(std::move(data_source)) {}

Mpeg2PsDemuxer::~Mpeg2PsDemuxer() = default;

const char* Mpeg2PsDemuxer::name() {
  return "Mpeg2PsDemuxer";
}

status_t Mpeg2PsDemuxer::Init() {
  if (initialized_) {
    return OK;
  }

  if (!mpeg2::SniffMpeg2Ps(data_source_)) {
    return media::ERROR_UNSUPPORTED;
  }

  source_format_ = media::MediaMeta::CreatePtr(
      media::MediaType::UNKNOWN, media::MediaMeta::FormatType::kTrack);
  source_format_->SetMime(media::MEDIA_MIMETYPE_CONTAINER_MPEG2PS);

  buffer_ = std::make_shared<media::Buffer>(kChunkSize);
  buffer_->setRange(0, 0);

  status_t last_err = OK;
  while (offset_ < kMaxInitBytes) {
    last_err = FeedMore();
    if (HasMediaType(media::MediaType::VIDEO) &&
        HasMediaType(media::MediaType::AUDIO)) {
      break;
    }
    if (last_err != OK) {
      break;
    }
  }

  scanning_ = false;
  if (visible_track_ids_.empty()) {
    return last_err == OK ? media::ERROR_UNSUPPORTED : last_err;
  }

  initialized_ = true;
  return OK;
}

status_t Mpeg2PsDemuxer::GetFormat(std::shared_ptr<media::MediaMeta>& format) {
  if (!initialized_) {
    return NO_INIT;
  }
  format = source_format_;
  return OK;
}

size_t Mpeg2PsDemuxer::GetTrackCount() {
  return visible_track_ids_.size();
}

status_t Mpeg2PsDemuxer::GetTrackFormat(
    std::shared_ptr<media::MediaMeta>& format,
    size_t track_index) {
  if (!initialized_) {
    return NO_INIT;
  }
  if (track_index >= visible_track_ids_.size()) {
    return BAD_VALUE;
  }

  auto it = tracks_by_id_.find(visible_track_ids_[track_index]);
  if (it == tracks_by_id_.end() || !it->second->source) {
    return NO_INIT;
  }

  format = it->second->source->GetFormat();
  return format != nullptr ? OK : NO_INIT;
}

std::shared_ptr<media::MediaSource> Mpeg2PsDemuxer::GetTrack(
    size_t track_index) {
  if (!initialized_ || track_index >= visible_track_ids_.size()) {
    return nullptr;
  }

  auto it = tracks_by_id_.find(visible_track_ids_[track_index]);
  if (it == tracks_by_id_.end() || !it->second->source) {
    return nullptr;
  }

  auto packet_source = it->second->source;
  auto self = shared_from_this();
  return std::make_shared<mpeg2::PacketSourceTrack>(
      packet_source, [self, packet_source]() {
        return self->FeedUntilBufferAvailable(packet_source);
      });
}

bool Mpeg2PsDemuxer::HasMediaType(media::MediaType type) const {
  for (unsigned stream_id : visible_track_ids_) {
    auto it = tracks_by_id_.find(stream_id);
    if (it != tracks_by_id_.end() && it->second->source != nullptr) {
      auto format = it->second->source->GetFormat();
      if (format != nullptr && format->stream_type() == type) {
        return true;
      }
    }
  }
  return false;
}

void Mpeg2PsDemuxer::MaybeAddVisibleTrack(unsigned stream_id) {
  if (std::find(visible_track_ids_.begin(), visible_track_ids_.end(),
                stream_id) != visible_track_ids_.end()) {
    return;
  }
  visible_track_ids_.push_back(stream_id);
}

status_t Mpeg2PsDemuxer::FeedMore() {
  for (;;) {
    ssize_t consumed = DequeueChunk();
    if (consumed == -EAGAIN) {
      if (final_result_ != OK) {
        SignalEOSToTracks(final_result_);
        return final_result_;
      }

      if (buffer_->offset() > 0 && buffer_->size() > 0) {
        std::memmove(buffer_->base(), buffer_->data(), buffer_->size());
        buffer_->setRange(0, buffer_->size());
      } else if (buffer_->size() == 0) {
        buffer_->setRange(0, 0);
      }

      if (buffer_->capacity() - buffer_->size() < kChunkSize) {
        buffer_->ensureCapacity(buffer_->capacity() + kChunkSize, true);
        buffer_->setRange(0, buffer_->size());
      }

      ssize_t bytes_read = data_source_->ReadAt(
          offset_, buffer_->data() + buffer_->size(), kChunkSize);
      if (bytes_read <= 0) {
        final_result_ = bytes_read < 0 ? static_cast<status_t>(bytes_read)
                                       : media::ERROR_END_OF_STREAM;
        continue;
      }

      buffer_->setRange(0, buffer_->size() + bytes_read);
      offset_ += bytes_read;
      if (bytes_read < static_cast<ssize_t>(kChunkSize)) {
        final_result_ = media::ERROR_END_OF_STREAM;
      }
      continue;
    }

    if (consumed < 0) {
      SignalEOSToTracks(static_cast<status_t>(consumed));
      return static_cast<status_t>(consumed);
    }

    if (buffer_->size() < static_cast<size_t>(consumed)) {
      return media::ERROR_MALFORMED;
    }

    buffer_->setRange(buffer_->offset() + consumed, buffer_->size() - consumed);
    return OK;
  }
}

status_t Mpeg2PsDemuxer::FeedUntilBufferAvailable(
    const std::shared_ptr<media::mpeg2ts::PacketSource>& source) {
  for (;;) {
    status_t final_result = OK;
    if (source->HasBufferAvailable(&final_result)) {
      return OK;
    }

    if (final_result != OK) {
      return final_result;
    }

    status_t err = FeedMore();
    if (err != OK && err != media::ERROR_END_OF_STREAM) {
      return err;
    }
  }
}

ssize_t Mpeg2PsDemuxer::DequeueChunk() {
  if (buffer_->size() < 4) {
    return -EAGAIN;
  }

  if (std::memcmp("\x00\x00\x01", buffer_->data(), 3) != 0) {
    return 1;
  }

  const unsigned chunk_type = buffer_->data()[3];
  switch (chunk_type) {
    case 0xB9:
      final_result_ = media::ERROR_END_OF_STREAM;
      return 4;
    case 0xBA:
      return DequeuePack();
    case 0xBB:
      return DequeueSystemHeader();
    default:
      return DequeuePES();
  }
}

ssize_t Mpeg2PsDemuxer::DequeuePack() {
  if (buffer_->size() < 14) {
    return -EAGAIN;
  }

  const unsigned pack_stuffing_length = buffer_->data()[13] & 0x07;
  return 14 + pack_stuffing_length;
}

ssize_t Mpeg2PsDemuxer::DequeueSystemHeader() {
  if (buffer_->size() < 6) {
    return -EAGAIN;
  }

  const unsigned header_length = (buffer_->data()[4] << 8) | buffer_->data()[5];
  return 6 + header_length;
}

ssize_t Mpeg2PsDemuxer::DequeuePES() {
  if (buffer_->size() < 6) {
    return -EAGAIN;
  }

  const uint8_t* data = buffer_->data();
  const unsigned pes_packet_length = (data[4] << 8) | data[5];

  size_t packet_size = 0;
  if (pes_packet_length == 0u) {
    ssize_t next_start = FindStartCodeOffset(data + 6, buffer_->size() - 6);
    if (next_start < 0) {
      if (final_result_ == OK) {
        return -EAGAIN;
      }
      packet_size = buffer_->size();
    } else {
      packet_size = 6 + static_cast<size_t>(next_start);
    }
  } else {
    packet_size = pes_packet_length + 6;
    if (buffer_->size() < packet_size) {
      return -EAGAIN;
    }
  }

  media::BitReader br(data, packet_size);
  if (br.numBitsLeft() < 48) {
    return media::ERROR_MALFORMED;
  }

  const uint32_t packet_start_code_prefix = br.getBits(24);
  if (packet_start_code_prefix != 1) {
    return media::ERROR_MALFORMED;
  }

  const unsigned stream_id = br.getBits(8);
  br.getBits(16);  // PES_packet_length

  if (stream_id == 0xBC) {
    if (br.numBitsLeft() < 16) {
      return media::ERROR_MALFORMED;
    }

    br.getBits(1);  // current_next_indicator
    br.getBits(2);  // reserved
    br.getBits(5);  // program_stream_map_version
    br.getBits(7);  // reserved
    br.getBits(1);  // marker bit
    const unsigned program_stream_info_length = br.getBits(16);

    size_t offset = 0;
    while (offset < program_stream_info_length) {
      if (offset + 2 > program_stream_info_length || br.numBitsLeft() < 16) {
        return media::ERROR_MALFORMED;
      }
      br.getBits(8);  // descriptor_tag
      const unsigned descriptor_length = br.getBits(8);
      if (offset + 2 + descriptor_length > program_stream_info_length ||
          br.numBitsLeft() < descriptor_length * 8) {
        return media::ERROR_MALFORMED;
      }
      br.skipBits(descriptor_length * 8);
      offset += 2 + descriptor_length;
    }

    if (br.numBitsLeft() < 16) {
      return media::ERROR_MALFORMED;
    }
    const unsigned elementary_stream_map_length = br.getBits(16);
    offset = 0;
    while (offset < elementary_stream_map_length) {
      if (offset + 4 > elementary_stream_map_length || br.numBitsLeft() < 32) {
        return media::ERROR_MALFORMED;
      }
      const unsigned stream_type = br.getBits(8);
      const unsigned elementary_stream_id = br.getBits(8);
      const unsigned info_length = br.getBits(16);
      if (offset + 4 + info_length > elementary_stream_map_length ||
          br.numBitsLeft() < info_length * 8) {
        return media::ERROR_MALFORMED;
      }
      stream_type_by_esid_[elementary_stream_id] = stream_type;
      br.skipBits(info_length * 8);
      offset += 4 + info_length;
    }

    if (br.numBitsLeft() < 32) {
      return media::ERROR_MALFORMED;
    }
    br.getBits(32);  // CRC32
    program_stream_map_valid_ = true;
    return packet_size;
  }

  if (stream_id == 0xBE || stream_id == 0xBF || stream_id == 0xF0 ||
      stream_id == 0xF1 || stream_id == 0xFF || stream_id == 0xF2 ||
      stream_id == 0xF8) {
    return packet_size;
  }

  if (br.numBitsLeft() < 24) {
    return media::ERROR_MALFORMED;
  }

  const unsigned marker_bits = br.getBits(2);
  if (marker_bits != 0x2) {
    return media::ERROR_MALFORMED;
  }

  br.getBits(2);  // PES_scrambling_control
  br.getBits(1);  // PES_priority
  br.getBits(1);  // data_alignment_indicator
  br.getBits(1);  // copyright
  br.getBits(1);  // original_or_copy

  const unsigned pts_dts_flags = br.getBits(2);
  const unsigned escr_flag = br.getBits(1);
  const unsigned es_rate_flag = br.getBits(1);
  br.getBits(1);  // DSM_trick_mode_flag
  br.getBits(1);  // additional_copy_info_flag
  br.getBits(1);  // PES_CRC_flag
  br.getBits(1);  // PES_extension_flag

  unsigned header_data_length = br.getBits(8);
  uint64_t pts = 0;

  if (pts_dts_flags == 2 || pts_dts_flags == 3) {
    if (header_data_length < 5 || br.numBitsLeft() < 40) {
      return media::ERROR_MALFORMED;
    }

    if (br.getBits(4) != pts_dts_flags) {
      return media::ERROR_MALFORMED;
    }

    pts = static_cast<uint64_t>(br.getBits(3)) << 30;
    if (br.getBits(1) != 1u) {
      return media::ERROR_MALFORMED;
    }
    pts |= static_cast<uint64_t>(br.getBits(15)) << 15;
    if (br.getBits(1) != 1u) {
      return media::ERROR_MALFORMED;
    }
    pts |= br.getBits(15);
    if (br.getBits(1) != 1u) {
      return media::ERROR_MALFORMED;
    }

    header_data_length -= 5;
    if (pts_dts_flags == 3) {
      if (header_data_length < 5 || br.numBitsLeft() < 40) {
        return media::ERROR_MALFORMED;
      }
      br.skipBits(40);
      header_data_length -= 5;
    }
  }

  if (escr_flag) {
    if (header_data_length < 6 || br.numBitsLeft() < 48) {
      return media::ERROR_MALFORMED;
    }
    br.skipBits(48);
    header_data_length -= 6;
  }

  if (es_rate_flag) {
    if (header_data_length < 3 || br.numBitsLeft() < 24) {
      return media::ERROR_MALFORMED;
    }
    br.skipBits(24);
    header_data_length -= 3;
  }

  if (br.numBitsLeft() < header_data_length * 8) {
    return media::ERROR_MALFORMED;
  }
  br.skipBits(header_data_length * 8);

  if (packet_size < 9) {
    return media::ERROR_MALFORMED;
  }
  const size_t data_length = packet_size - 9 - data[8];
  if (br.numBitsLeft() < data_length * 8) {
    return media::ERROR_MALFORMED;
  }

  TrackState* track = GetOrCreateTrack(stream_id);
  status_t err = OK;
  if (track != nullptr) {
    err = AppendPesData(track, pts_dts_flags == 2 || pts_dts_flags == 3, pts,
                        br.data(), data_length);
  }

  return err == OK ? static_cast<ssize_t>(packet_size) : err;
}

Mpeg2PsDemuxer::TrackState* Mpeg2PsDemuxer::GetOrCreateTrack(
    unsigned stream_id) {
  auto it = tracks_by_id_.find(stream_id);
  if (it != tracks_by_id_.end()) {
    return it->second.get();
  }

  const unsigned stream_type = GuessStreamType(stream_id);
  auto queue = CreateQueueForStreamType(stream_type);
  if (!queue) {
    return nullptr;
  }

  auto track = std::make_unique<TrackState>();
  track->stream_id = stream_id;
  track->stream_type = stream_type;
  track->queue = std::move(queue);

  TrackState* track_ptr = track.get();
  tracks_by_id_[stream_id] = std::move(track);
  return track_ptr;
}

unsigned Mpeg2PsDemuxer::GuessStreamType(unsigned stream_id) const {
  if (program_stream_map_valid_) {
    auto it = stream_type_by_esid_.find(stream_id);
    if (it != stream_type_by_esid_.end()) {
      return it->second;
    }
  }

  if ((stream_id & ~0x1F) == 0xC0) {
    return media::mpeg2ts::TSParser::STREAMTYPE_MPEG2_AUDIO;
  }
  if ((stream_id & ~0x0F) == 0xE0) {
    return media::mpeg2ts::TSParser::STREAMTYPE_MPEG2_VIDEO;
  }

  return media::mpeg2ts::TSParser::STREAMTYPE_RESERVED;
}

std::unique_ptr<media::mpeg2ts::ESQueue>
Mpeg2PsDemuxer::CreateQueueForStreamType(unsigned stream_type) const {
  using TSParser = media::mpeg2ts::TSParser;
  using ESQueue = media::mpeg2ts::ESQueue;

  ESQueue::Mode mode = ESQueue::Mode::INVALID;
  switch (stream_type) {
    case TSParser::STREAMTYPE_H264:
      mode = ESQueue::Mode::H264;
      break;
    case TSParser::STREAMTYPE_H265:
      mode = ESQueue::Mode::HEVC;
      break;
    case TSParser::STREAMTYPE_MPEG2_AUDIO_ADTS:
      mode = ESQueue::Mode::AAC;
      break;
    case TSParser::STREAMTYPE_MPEG1_AUDIO:
    case TSParser::STREAMTYPE_MPEG2_AUDIO:
      mode = ESQueue::Mode::MPEG_AUDIO;
      break;
    case TSParser::STREAMTYPE_MPEG1_VIDEO:
    case TSParser::STREAMTYPE_MPEG2_VIDEO:
      mode = ESQueue::Mode::MPEG_VIDEO;
      break;
    case TSParser::STREAMTYPE_MPEG4_VIDEO:
      mode = ESQueue::Mode::MPEG4_VIDEO;
      break;
    case TSParser::STREAMTYPE_AC3:
      mode = ESQueue::Mode::AC3;
      break;
    case TSParser::STREAMTYPE_EAC3:
      mode = ESQueue::Mode::EAC3;
      break;
    default:
      break;
  }

  if (mode == ESQueue::Mode::INVALID) {
    return nullptr;
  }
  return std::make_unique<ESQueue>(mode);
}

status_t Mpeg2PsDemuxer::AppendPesData(TrackState* track,
                                       bool has_pts,
                                       uint64_t pts,
                                       const uint8_t* data,
                                       size_t size) {
  if (track == nullptr || track->queue == nullptr) {
    return OK;
  }

  const int64_t time_us =
      has_pts ? static_cast<int64_t>((pts * 1000000ull) / 90000ull) : -1;

  status_t err = track->queue->AppendData(data, size, time_us);
  if (err != OK) {
    return err;
  }

  DrainTrack(track);
  return OK;
}

void Mpeg2PsDemuxer::DrainTrack(TrackState* track) {
  for (;;) {
    auto access_unit = track->queue->DequeueAccessUnit();
    if (!access_unit) {
      break;
    }

    auto format = track->queue->GetFormat();
    if (!track->source && format) {
      track->source = std::make_shared<media::mpeg2ts::PacketSource>(format);
      MaybeAddVisibleTrack(track->stream_id);
    }

    if (track->source && format) {
      track->source->SetFormat(format);
      track->source->QueueAccessUnit(access_unit);
    }
  }
}

void Mpeg2PsDemuxer::SignalEOSToTracks(status_t final_result) {
  if (eos_signaled_) {
    return;
  }

  for (auto& [stream_id, track] : tracks_by_id_) {
    if (track->queue) {
      track->queue->SignalEOS();
      DrainTrack(track.get());
    }
    if (track->source) {
      track->source->SignalEOS(final_result);
    }
  }

  eos_signaled_ = true;
}

}  // namespace player
}  // namespace ave
