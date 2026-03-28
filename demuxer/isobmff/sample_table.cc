/*
 * sample_table.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "sample_table.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "base/logging.h"
#include "media/foundation/media_errors.h"

namespace ave {
namespace isobmff {

using media::ERROR_END_OF_STREAM;
using media::ERROR_IO;
using media::ERROR_MALFORMED;
using media::ERROR_OUT_OF_RANGE;

SampleTable::SampleTable(DataSourceBase* source) : source_(source) {}

SampleTable::~SampleTable() = default;

// ---------- stts (time-to-sample) ----------

status_t SampleTable::SetTimeToSampleParams(off64_t data_offset,
                                            size_t data_size) {
  // FullBox header (version+flags) already consumed by caller.
  // data_offset points to entry_count.
  if (data_size < 4) {
    return ERROR_MALFORMED;
  }

  uint32_t entry_count = 0;
  if (!source_->GetUInt32(data_offset, &entry_count)) {
    return ERROR_IO;
  }

  if (entry_count > data_size / 8) {
    return ERROR_MALFORMED;
  }

  stts_entries_.resize(entry_count);
  num_samples_ = 0;

  for (uint32_t i = 0; i < entry_count; i++) {
    off64_t entry_offset = data_offset + 4 + i * 8;
    uint32_t sample_count = 0;
    uint32_t sample_delta = 0;
    if (!source_->GetUInt32(entry_offset, &sample_count) ||
        !source_->GetUInt32(entry_offset + 4, &sample_delta)) {
      return ERROR_IO;
    }

    stts_entries_[i] = {sample_count, sample_delta};

    if (num_samples_ + sample_count < num_samples_) {
      // overflow
      return ERROR_MALFORMED;
    }
    num_samples_ += sample_count;
  }

  AVE_LOG(LS_INFO) << "stts: " << entry_count << " entries, " << num_samples_
                   << " total samples";
  return OK;
}

// ---------- ctts (composition time offset) ----------

status_t SampleTable::SetCompositionTimeToSampleParams(off64_t data_offset,
                                                       size_t data_size) {
  if (data_size < 4) {
    return ERROR_MALFORMED;
  }

  uint32_t entry_count = 0;
  if (!source_->GetUInt32(data_offset, &entry_count)) {
    return ERROR_IO;
  }

  if (entry_count > data_size / 8) {
    return ERROR_MALFORMED;
  }

  ctts_entries_.resize(entry_count);

  for (uint32_t i = 0; i < entry_count; i++) {
    off64_t entry_offset = data_offset + 4 + i * 8;
    uint32_t sample_count = 0;
    uint32_t raw_offset = 0;
    if (!source_->GetUInt32(entry_offset, &sample_count) ||
        !source_->GetUInt32(entry_offset + 4, &raw_offset)) {
      return ERROR_IO;
    }

    ctts_entries_[i] = {sample_count, static_cast<int32_t>(raw_offset)};
  }

  has_ctts_ = true;
  AVE_LOG(LS_INFO) << "ctts: " << entry_count << " entries";
  return OK;
}

// ---------- stsc (sample-to-chunk) ----------

status_t SampleTable::SetSampleToChunkParams(off64_t data_offset,
                                             size_t data_size) {
  if (data_size < 4) {
    return ERROR_MALFORMED;
  }

  uint32_t entry_count = 0;
  if (!source_->GetUInt32(data_offset, &entry_count)) {
    return ERROR_IO;
  }

  if (entry_count > data_size / 12) {
    return ERROR_MALFORMED;
  }

  stsc_entries_.resize(entry_count);

  for (uint32_t i = 0; i < entry_count; i++) {
    off64_t entry_offset = data_offset + 4 + i * 12;
    uint32_t first_chunk = 0;
    uint32_t samples_per_chunk = 0;
    uint32_t sample_desc_index = 0;
    if (!source_->GetUInt32(entry_offset, &first_chunk) ||
        !source_->GetUInt32(entry_offset + 4, &samples_per_chunk) ||
        !source_->GetUInt32(entry_offset + 8, &sample_desc_index)) {
      return ERROR_IO;
    }

    if (first_chunk == 0) {
      return ERROR_MALFORMED;  // 1-based in file
    }
    stsc_entries_[i] = {first_chunk - 1, samples_per_chunk, sample_desc_index};
  }

  AVE_LOG(LS_INFO) << "stsc: " << entry_count << " entries";
  return OK;
}

// ---------- stsz / stz2 (sample sizes) ----------

status_t SampleTable::SetSampleSizeParams(off64_t data_offset,
                                          size_t data_size) {
  // For stsz: version(1)+flags(3) already consumed.
  // data_offset → sample_size(4) | sample_count(4) | [sizes...]
  if (data_size < 8) {
    return ERROR_MALFORMED;
  }

  uint32_t default_size = 0;
  uint32_t sample_count = 0;
  if (!source_->GetUInt32(data_offset, &default_size) ||
      !source_->GetUInt32(data_offset + 4, &sample_count)) {
    return ERROR_IO;
  }

  default_sample_size_ = default_size;
  stsz_field_size_ = 32;
  stsz_data_offset_ = data_offset + 8;

  if (num_samples_ == 0) {
    num_samples_ = sample_count;
  } else if (num_samples_ != sample_count) {
    AVE_LOG(LS_WARNING) << "stsz sample count " << sample_count
                        << " != stts count " << num_samples_;
  }

  AVE_LOG(LS_INFO) << "stsz: " << sample_count
                   << " samples, default_size=" << default_size;
  return OK;
}

// ---------- stco / co64 (chunk offsets) ----------

status_t SampleTable::SetChunkOffsetParams(off64_t data_offset,
                                           size_t data_size,
                                           bool is_co64) {
  if (data_size < 4) {
    return ERROR_MALFORMED;
  }

  uint32_t entry_count = 0;
  if (!source_->GetUInt32(data_offset, &entry_count)) {
    return ERROR_IO;
  }

  num_chunk_offsets_ = entry_count;
  chunk_offset_data_offset_ = data_offset + 4;
  chunk_offset_is_64bit_ = is_co64;

  AVE_LOG(LS_INFO) << (is_co64 ? "co64" : "stco") << ": " << entry_count
                   << " chunks";
  return OK;
}

// ---------- stss (sync samples) ----------

status_t SampleTable::SetSyncSampleParams(off64_t data_offset,
                                          size_t data_size) {
  if (data_size < 4) {
    return ERROR_MALFORMED;
  }

  uint32_t entry_count = 0;
  if (!source_->GetUInt32(data_offset, &entry_count)) {
    return ERROR_IO;
  }

  if (entry_count > data_size / 4) {
    return ERROR_MALFORMED;
  }

  sync_samples_.resize(entry_count);

  for (uint32_t i = 0; i < entry_count; i++) {
    uint32_t sample_number = 0;
    if (!source_->GetUInt32(data_offset + 4 + i * 4, &sample_number)) {
      return ERROR_IO;
    }
    if (sample_number == 0) {
      return ERROR_MALFORMED;
    }
    sync_samples_[i] = sample_number - 1;  // convert to 0-based
  }

  has_sync_table_ = true;
  AVE_LOG(LS_INFO) << "stss: " << entry_count << " sync samples";
  return OK;
}

// ---------- Sample lookup ----------

status_t SampleTable::GetSampleSize(uint32_t sample_index, uint32_t* size) {
  if (sample_index >= num_samples_) {
    return ERROR_OUT_OF_RANGE;
  }

  if (default_sample_size_ > 0) {
    *size = default_sample_size_;
    return OK;
  }

  if (stsz_field_size_ == 32) {
    bool ok = source_->GetUInt32(
        stsz_data_offset_ + static_cast<off64_t>(sample_index) * 4, size);
    return ok ? OK : static_cast<status_t>(ERROR_IO);
  }

  // Compact sizes (stz2) — 4, 8, 16 bit
  if (stsz_field_size_ == 16) {
    uint16_t val = 0;
    if (!source_->GetUInt16(
            stsz_data_offset_ + static_cast<off64_t>(sample_index) * 2, &val)) {
      return ERROR_IO;
    }
    *size = val;
    return OK;
  }

  if (stsz_field_size_ == 8) {
    uint8_t val = 0;
    if (source_->ReadAt(stsz_data_offset_ + static_cast<off64_t>(sample_index),
                        &val, 1) != 1) {
      return ERROR_IO;
    }
    *size = val;
    return OK;
  }

  // 4-bit
  uint8_t val = 0;
  if (source_->ReadAt(
          stsz_data_offset_ + static_cast<off64_t>(sample_index / 2), &val,
          1) != 1) {
    return ERROR_IO;
  }
  *size = (sample_index & 1) ? (val & 0x0f) : (val >> 4);
  return OK;
}

status_t SampleTable::GetChunkOffset(uint32_t chunk_index, off64_t* offset) {
  if (chunk_index >= num_chunk_offsets_) {
    return ERROR_OUT_OF_RANGE;
  }

  if (chunk_offset_is_64bit_) {
    uint64_t val = 0;
    if (!source_->GetUInt64(
            chunk_offset_data_offset_ + static_cast<off64_t>(chunk_index) * 8,
            &val)) {
      return ERROR_IO;
    }
    *offset = static_cast<off64_t>(val);
  } else {
    uint32_t val = 0;
    if (!source_->GetUInt32(
            chunk_offset_data_offset_ + static_cast<off64_t>(chunk_index) * 4,
            &val)) {
      return ERROR_IO;
    }
    *offset = static_cast<off64_t>(val);
  }

  return OK;
}

status_t SampleTable::FindChunkAndOffsetForSample(uint32_t sample_index,
                                                  off64_t* sample_offset,
                                                  uint32_t* sample_size) {
  if (stsc_entries_.empty()) {
    return ERROR_MALFORMED;
  }

  // Find the stsc entry range that contains this sample.
  uint32_t first_sample_in_range = 0;
  size_t stsc_idx = 0;

  for (size_t i = 0; i < stsc_entries_.size(); i++) {
    uint32_t first_chunk = stsc_entries_[i].first_chunk;
    uint32_t next_first_chunk = (i + 1 < stsc_entries_.size())
                                    ? stsc_entries_[i + 1].first_chunk
                                    : num_chunk_offsets_;

    uint32_t num_chunks_in_range = next_first_chunk - first_chunk;
    uint32_t samples_in_range =
        num_chunks_in_range * stsc_entries_[i].samples_per_chunk;

    if (sample_index < first_sample_in_range + samples_in_range) {
      stsc_idx = i;
      break;
    }
    first_sample_in_range += samples_in_range;
    if (i + 1 == stsc_entries_.size()) {
      return ERROR_OUT_OF_RANGE;
    }
  }

  uint32_t samples_per_chunk = stsc_entries_[stsc_idx].samples_per_chunk;
  if (samples_per_chunk == 0) {
    return ERROR_MALFORMED;
  }
  uint32_t sample_in_range = sample_index - first_sample_in_range;
  uint32_t chunk_in_range = sample_in_range / samples_per_chunk;
  uint32_t sample_in_chunk = sample_in_range % samples_per_chunk;

  uint32_t chunk_index = stsc_entries_[stsc_idx].first_chunk + chunk_in_range;

  off64_t chunk_offset = 0;
  status_t err = GetChunkOffset(chunk_index, &chunk_offset);
  if (err != OK) {
    return err;
  }

  // Sum sizes of preceding samples in the same chunk.
  off64_t offset_in_chunk = 0;
  for (uint32_t i = 0; i < sample_in_chunk; i++) {
    uint32_t sz = 0;
    uint32_t idx = sample_index - sample_in_chunk + i;
    err = GetSampleSize(idx, &sz);
    if (err != OK) {
      return err;
    }
    offset_in_chunk += sz;
  }

  err = GetSampleSize(sample_index, sample_size);
  if (err != OK) {
    return err;
  }

  *sample_offset = chunk_offset + offset_in_chunk;
  return OK;
}

int64_t SampleTable::TicksToUs(int64_t ticks) const {
  if (timescale_ == 0) {
    return 0;
  }
  // Avoid overflow: use 128-bit intermediate via two 64-bit ops.
  return (ticks / timescale_) * 1000000LL +
         (ticks % timescale_) * 1000000LL / timescale_;
}

status_t SampleTable::GetSampleTime(uint32_t sample_index,
                                    int64_t* dts_us,
                                    int64_t* pts_us,
                                    int64_t* duration_us) {
  if (stts_entries_.empty()) {
    return ERROR_MALFORMED;
  }

  // Walk stts to find DTS
  int64_t dts_ticks = 0;
  uint32_t sample_cursor = 0;
  uint32_t stts_duration = 0;

  for (const auto& entry : stts_entries_) {
    if (sample_index < sample_cursor + entry.sample_count) {
      stts_duration = entry.sample_delta;
      dts_ticks += static_cast<int64_t>(sample_index - sample_cursor) *
                   entry.sample_delta;
      break;
    }
    dts_ticks += static_cast<int64_t>(entry.sample_count) * entry.sample_delta;
    sample_cursor += entry.sample_count;
  }

  *dts_us = TicksToUs(dts_ticks);
  *duration_us = TicksToUs(stts_duration);

  // Apply ctts offset for PTS
  if (has_ctts_) {
    int32_t ctts_offset = 0;
    uint32_t ctts_cursor = 0;
    for (const auto& entry : ctts_entries_) {
      if (sample_index < ctts_cursor + entry.sample_count) {
        ctts_offset = entry.sample_offset;
        break;
      }
      ctts_cursor += entry.sample_count;
    }
    *pts_us = TicksToUs(dts_ticks + ctts_offset);
  } else {
    *pts_us = *dts_us;
  }

  return OK;
}

bool SampleTable::IsSyncSample(uint32_t sample_index) const {
  if (!has_sync_table_) {
    return true;  // No stss means every sample is a sync sample
  }
  return std::binary_search(sync_samples_.begin(), sync_samples_.end(),
                            sample_index);
}

status_t SampleTable::GetSampleInfo(uint32_t sample_index, SampleInfo* info) {
  if (sample_index >= num_samples_) {
    return ERROR_OUT_OF_RANGE;
  }

  if (sample_index == cached_sample_index_) {
    *info = cached_info_;
    return OK;
  }

  off64_t sample_offset = 0;
  uint32_t sample_size = 0;
  status_t err =
      FindChunkAndOffsetForSample(sample_index, &sample_offset, &sample_size);
  if (err != OK) {
    return err;
  }

  int64_t dts_us = 0, pts_us = 0, duration_us = 0;
  err = GetSampleTime(sample_index, &dts_us, &pts_us, &duration_us);
  if (err != OK) {
    return err;
  }

  info->offset = sample_offset;
  info->size = sample_size;
  info->pts_us = pts_us;
  info->dts_us = dts_us;
  info->duration_us = duration_us;
  info->is_sync = IsSyncSample(sample_index);

  cached_sample_index_ = sample_index;
  cached_info_ = *info;

  return OK;
}

status_t SampleTable::SampleIndexForTime(int64_t time_us,
                                         uint32_t* sample_index) {
  if (stts_entries_.empty() || timescale_ == 0) {
    return ERROR_MALFORMED;
  }

  // Convert time to ticks
  int64_t target_ticks = time_us * timescale_ / 1000000LL;
  int64_t ticks = 0;
  uint32_t cursor = 0;

  for (const auto& entry : stts_entries_) {
    int64_t range_ticks =
        static_cast<int64_t>(entry.sample_count) * entry.sample_delta;
    if (ticks + range_ticks > target_ticks) {
      if (entry.sample_delta > 0) {
        *sample_index = cursor + static_cast<uint32_t>((target_ticks - ticks) /
                                                       entry.sample_delta);
      } else {
        *sample_index = cursor;
      }
      return OK;
    }
    ticks += range_ticks;
    cursor += entry.sample_count;
  }

  *sample_index = num_samples_ > 0 ? num_samples_ - 1 : 0;
  return OK;
}

status_t SampleTable::FindSyncSampleNear(int64_t time_us,
                                         uint32_t* sample_index,
                                         int flags) {
  uint32_t target_index = 0;
  status_t err = SampleIndexForTime(time_us, &target_index);
  if (err != OK) {
    return err;
  }

  if (!has_sync_table_) {
    // Every sample is sync
    *sample_index = target_index;
    return OK;
  }

  if (sync_samples_.empty()) {
    return ERROR_MALFORMED;
  }

  // Binary search in sorted sync_samples_
  auto it = std::lower_bound(sync_samples_.begin(), sync_samples_.end(),
                             target_index);

  if (flags == kFlagBefore) {
    if (it != sync_samples_.end() && *it == target_index) {
      *sample_index = *it;
    } else if (it != sync_samples_.begin()) {
      --it;
      *sample_index = *it;
    } else {
      *sample_index = sync_samples_.front();
    }
  } else if (flags == kFlagAfter) {
    if (it != sync_samples_.end()) {
      *sample_index = *it;
    } else {
      *sample_index = sync_samples_.back();
    }
  } else {
    // kFlagClosest
    uint32_t before = 0;
    uint32_t after = 0;

    if (it != sync_samples_.end()) {
      after = *it;
    } else {
      after = sync_samples_.back();
    }

    if (it != sync_samples_.begin()) {
      auto prev = it;
      --prev;
      before = *prev;
    } else {
      before = sync_samples_.front();
    }

    int64_t diff_before =
        target_index >= before ? target_index - before : before - target_index;
    int64_t diff_after =
        after >= target_index ? after - target_index : target_index - after;

    *sample_index = (diff_before <= diff_after) ? before : after;
  }

  return OK;
}

}  // namespace isobmff
}  // namespace ave
