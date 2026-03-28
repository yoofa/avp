/*
 * sample_table.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_ISOBMFF_SAMPLE_TABLE_H_
#define DEMUXER_ISOBMFF_SAMPLE_TABLE_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/data_source/data_source_base.h"
#include "base/errors.h"

namespace ave {
namespace isobmff {

// Information about a single sample retrieved from the sample table.
struct SampleInfo {
  off64_t offset;       // Byte offset in file
  uint32_t size;        // Sample size in bytes
  int64_t pts_us;       // Presentation timestamp in microseconds
  int64_t dts_us;       // Decode timestamp in microseconds
  int64_t duration_us;  // Duration in microseconds
  bool is_sync;         // Is this a sync (key) sample?
};

// Parses and provides random access to ISO BMFF sample tables.
// Supports stts, ctts, stsc, stsz/stz2, stco/co64, and stss boxes.
class SampleTable {
 public:
  explicit SampleTable(DataSourceBase* source);
  ~SampleTable();

  // Parse individual table boxes.  Each should be called once with the
  // data region of the corresponding box (offset past version+flags).
  status_t SetTimeToSampleParams(off64_t data_offset, size_t data_size);
  status_t SetCompositionTimeToSampleParams(off64_t data_offset,
                                            size_t data_size);
  status_t SetSampleToChunkParams(off64_t data_offset, size_t data_size);
  status_t SetSampleSizeParams(off64_t data_offset, size_t data_size);
  status_t SetChunkOffsetParams(off64_t data_offset,
                                size_t data_size,
                                bool is_co64);
  status_t SetSyncSampleParams(off64_t data_offset, size_t data_size);

  // Set track timescale (ticks per second from mdhd).
  void SetTimescale(uint32_t timescale) { timescale_ = timescale; }
  uint32_t timescale() const { return timescale_; }

  uint32_t CountSamples() const { return num_samples_; }

  // Retrieve info for a sample by its 0-based index.
  status_t GetSampleInfo(uint32_t sample_index, SampleInfo* info);

  // Find the sync sample at or before the given time (in microseconds).
  // Returns the sample index, or error.
  status_t FindSyncSampleNear(int64_t time_us,
                              uint32_t* sample_index,
                              int flags);

  // Seek flags for FindSyncSampleNear
  enum {
    kFlagBefore = 0,   // sync sample at or before time
    kFlagAfter = 1,    // sync sample at or after time
    kFlagClosest = 2,  // closest sync sample
  };

 private:
  DataSourceBase* source_;
  uint32_t timescale_ = 0;
  uint32_t num_samples_ = 0;

  // --- stts (time-to-sample) ---
  struct SttsEntry {
    uint32_t sample_count;
    uint32_t sample_delta;  // in timescale units
  };
  std::vector<SttsEntry> stts_entries_;

  // --- ctts (composition time offset) ---
  struct CttsEntry {
    uint32_t sample_count;
    int32_t sample_offset;  // signed, can be negative for B-frames
  };
  std::vector<CttsEntry> ctts_entries_;
  bool has_ctts_ = false;

  // --- stsc (sample-to-chunk) ---
  struct StscEntry {
    uint32_t first_chunk;  // 0-based
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
  };
  std::vector<StscEntry> stsc_entries_;

  // --- stsz / stz2 (sample sizes) ---
  uint32_t default_sample_size_ = 0;
  off64_t stsz_data_offset_ = 0;  // offset to the size array in file
  uint8_t stsz_field_size_ = 32;  // 4, 8, 16, or 32 bits

  // --- stco / co64 (chunk offsets) ---
  off64_t chunk_offset_data_offset_ = 0;
  uint32_t num_chunk_offsets_ = 0;
  bool chunk_offset_is_64bit_ = false;

  // --- stss (sync samples) ---
  std::vector<uint32_t> sync_samples_;  // 0-based sample indices
  bool has_sync_table_ = false;

  // --- Iterator state for sequential access ---
  uint32_t cached_sample_index_ = UINT32_MAX;
  SampleInfo cached_info_;

  // Internal helpers
  status_t GetSampleSize(uint32_t sample_index, uint32_t* size);
  status_t GetChunkOffset(uint32_t chunk_index, off64_t* offset);
  status_t FindChunkAndOffsetForSample(uint32_t sample_index,
                                       off64_t* sample_offset,
                                       uint32_t* sample_size);
  status_t GetSampleTime(uint32_t sample_index,
                         int64_t* dts_us,
                         int64_t* pts_us,
                         int64_t* duration_us);
  bool IsSyncSample(uint32_t sample_index) const;

  int64_t TicksToUs(int64_t ticks) const;
  status_t SampleIndexForTime(int64_t time_us, uint32_t* sample_index);
};

}  // namespace isobmff
}  // namespace ave

#endif  // DEMUXER_ISOBMFF_SAMPLE_TABLE_H_
