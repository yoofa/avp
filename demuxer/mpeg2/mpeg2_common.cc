/*
 * mpeg2_common.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "demuxer/mpeg2/mpeg2_common.h"

#include <array>

#include "base/logging.h"

namespace ave {
namespace player {
namespace mpeg2 {

namespace {

constexpr size_t kTsPacketSize = 188;
constexpr size_t kM2tsPacketSize = 192;
constexpr size_t kTsSniffPacketCount = 5;
constexpr size_t kMaxTsSyncOffset = kM2tsPacketSize - 1;

bool HasSyncPattern(const uint8_t* data,
                    size_t size,
                    size_t sync_offset,
                    size_t packet_size) {
  const size_t required =
      sync_offset + (kTsSniffPacketCount - 1) * packet_size + 1;
  if (size < required) {
    return false;
  }

  for (size_t i = 0; i < kTsSniffPacketCount; ++i) {
    if (data[sync_offset + i * packet_size] != 0x47) {
      return false;
    }
  }
  return true;
}

}  // namespace

PacketSourceTrack::PacketSourceTrack(
    std::shared_ptr<media::mpeg2ts::PacketSource> source,
    EnsureDataFn ensure_data_fn)
    : source_(std::move(source)), ensure_data_fn_(std::move(ensure_data_fn)) {}

status_t PacketSourceTrack::Start(std::shared_ptr<media::Message> params) {
  started_ = true;
  return source_->Start(std::move(params));
}

status_t PacketSourceTrack::Stop() {
  started_ = false;
  return source_->Stop();
}

std::shared_ptr<media::MediaMeta> PacketSourceTrack::GetFormat() {
  return source_->GetFormat();
}

status_t PacketSourceTrack::Read(std::shared_ptr<media::MediaFrame>& frame,
                                 const ReadOptions* options) {
  if (!started_) {
    return NO_INIT;
  }

  if (options != nullptr) {
    int64_t seek_time_us = 0;
    ReadOptions::SeekMode seek_mode = ReadOptions::SEEK_CLOSEST_SYNC;
    if (options->GetSeekTo(&seek_time_us, &seek_mode)) {
      return media::ERROR_UNSUPPORTED;
    }
  }

  for (;;) {
    status_t err = ensure_data_fn_();
    if (err != OK && err != media::ERROR_END_OF_STREAM) {
      return err;
    }

    err = source_->Read(frame, nullptr);
    if (err == media::INFO_DISCONTINUITY) {
      waiting_for_video_sync_ = true;
      return err;
    }
    if (err != OK) {
      return err;
    }

    if (frame == nullptr || frame->stream_type() != media::MediaType::VIDEO) {
      return OK;
    }
    waiting_for_video_sync_ = false;
    return OK;
  }
}

bool SniffMpeg2Ts(std::shared_ptr<ave::DataSource> data_source,
                  TsPacketLayout* layout) {
  std::array<uint8_t, kMaxTsSyncOffset + kTsSniffPacketCount * kM2tsPacketSize>
      probe = {};
  ssize_t bytes_read = data_source->ReadAt(0, probe.data(), probe.size());
  if (bytes_read < static_cast<ssize_t>(kTsPacketSize * kTsSniffPacketCount)) {
    return false;
  }

  for (size_t sync_offset = 0; sync_offset <= kMaxTsSyncOffset; ++sync_offset) {
    if (HasSyncPattern(probe.data(), bytes_read, sync_offset, kTsPacketSize)) {
      layout->sync_offset = sync_offset;
      layout->packet_stride = kTsPacketSize;
      return true;
    }

    if (HasSyncPattern(probe.data(), bytes_read, sync_offset,
                       kM2tsPacketSize)) {
      layout->sync_offset = sync_offset;
      layout->packet_stride = kM2tsPacketSize;
      return true;
    }
  }

  return false;
}

bool SniffMpeg2Ps(std::shared_ptr<ave::DataSource> data_source) {
  std::array<uint8_t, 14> scratch = {};
  if (data_source->ReadAt(0, scratch.data(), scratch.size()) !=
      static_cast<ssize_t>(scratch.size())) {
    return false;
  }

  const uint32_t pack_start_code =
      (scratch[0] << 24) | (scratch[1] << 16) | (scratch[2] << 8) | scratch[3];
  if (pack_start_code != 0x000001BAu) {
    return false;
  }

  if ((scratch[4] & 0xC4) != 0x44) {
    return false;
  }
  if ((scratch[6] & 0x04) != 0x04) {
    return false;
  }
  if ((scratch[8] & 0x04) != 0x04) {
    return false;
  }
  if ((scratch[9] & 0x01) != 0x01) {
    return false;
  }
  if ((scratch[12] & 0x03) != 0x03) {
    return false;
  }

  const size_t stuffing_length = scratch[13] & 0x07;
  std::array<uint8_t, 3> prefix = {};
  if (data_source->ReadAt(14 + stuffing_length, prefix.data(), prefix.size()) !=
      static_cast<ssize_t>(prefix.size())) {
    return false;
  }

  const uint32_t packet_prefix =
      (prefix[0] << 16) | (prefix[1] << 8) | prefix[2];
  return packet_prefix == 0x000001u;
}

}  // namespace mpeg2
}  // namespace player
}  // namespace ave
