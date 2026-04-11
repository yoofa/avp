/*
 * mpeg2_ts_demuxer.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_MPEG2_MPEG2_TS_DEMUXER_H_
#define DEMUXER_MPEG2_MPEG2_TS_DEMUXER_H_

#include <memory>
#include <vector>

#include "api/demuxer/demuxer.h"

namespace ave {
namespace media {
namespace mpeg2ts {
class PacketSource;
class TSParser;
}  // namespace mpeg2ts
}  // namespace media
}  // namespace ave

namespace ave {
namespace player {

class Mpeg2TsDemuxer : public Demuxer,
                       public std::enable_shared_from_this<Mpeg2TsDemuxer> {
 public:
  explicit Mpeg2TsDemuxer(std::shared_ptr<ave::DataSource> data_source);
  ~Mpeg2TsDemuxer() override;

  status_t Init();

  status_t GetFormat(std::shared_ptr<MediaMeta>& format) override;
  size_t GetTrackCount() override;
  status_t GetTrackFormat(std::shared_ptr<MediaMeta>& format,
                          size_t track_index) override;
  std::shared_ptr<MediaSource> GetTrack(size_t track_index) override;
  const char* name() override;

 private:
  struct TrackEntry {
    unsigned source_type = 0;
    std::shared_ptr<media::mpeg2ts::PacketSource> packet_source;
  };

  bool HasTrack(unsigned type) const;
  void MaybeAddTrack(unsigned type);
  status_t FeedMore();
  status_t FeedUntilBufferAvailable(
      const std::shared_ptr<media::mpeg2ts::PacketSource>& source);

  std::shared_ptr<MediaMeta> source_format_;
  std::vector<TrackEntry> tracks_;
  std::unique_ptr<media::mpeg2ts::TSParser> parser_;
  off64_t offset_ = 0;
  off64_t size_ = 0;
  size_t sync_offset_ = 0;
  size_t packet_stride_ = 188;
  bool initialized_ = false;
  bool eos_signaled_ = false;
};

}  // namespace player
}  // namespace ave

#endif  // DEMUXER_MPEG2_MPEG2_TS_DEMUXER_H_
