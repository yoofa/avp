/*
 * mpeg2_ps_demuxer.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_MPEG2_MPEG2_PS_DEMUXER_H_
#define DEMUXER_MPEG2_MPEG2_PS_DEMUXER_H_

#include <map>
#include <memory>
#include <vector>

#include "api/demuxer/demuxer.h"

namespace ave {
namespace media {
class Buffer;
namespace mpeg2ts {
class ESQueue;
class PacketSource;
class TSParser;
}  // namespace mpeg2ts
}  // namespace media

namespace player {

class Mpeg2PsDemuxer : public Demuxer,
                       public std::enable_shared_from_this<Mpeg2PsDemuxer> {
 public:
  explicit Mpeg2PsDemuxer(std::shared_ptr<ave::DataSource> data_source);
  ~Mpeg2PsDemuxer() override;

  status_t Init();

  status_t GetFormat(std::shared_ptr<MediaMeta>& format) override;
  size_t GetTrackCount() override;
  status_t GetTrackFormat(std::shared_ptr<MediaMeta>& format,
                          size_t track_index) override;
  std::shared_ptr<MediaSource> GetTrack(size_t track_index) override;
  const char* name() override;

 private:
  struct TrackState {
    unsigned stream_id = 0;
    unsigned stream_type = 0;
    std::unique_ptr<media::mpeg2ts::ESQueue> queue;
    std::shared_ptr<media::mpeg2ts::PacketSource> source;
  };

  bool HasMediaType(ave::media::MediaType type) const;
  void MaybeAddVisibleTrack(unsigned stream_id);
  status_t FeedMore();
  status_t FeedUntilBufferAvailable(
      const std::shared_ptr<media::mpeg2ts::PacketSource>& source);

  ssize_t DequeueChunk();
  ssize_t DequeuePack();
  ssize_t DequeueSystemHeader();
  ssize_t DequeuePES();

  TrackState* GetOrCreateTrack(unsigned stream_id);
  unsigned GuessStreamType(unsigned stream_id) const;
  std::unique_ptr<media::mpeg2ts::ESQueue> CreateQueueForStreamType(
      unsigned stream_type) const;
  status_t AppendPesData(TrackState* track,
                         bool has_pts,
                         uint64_t pts,
                         const uint8_t* data,
                         size_t size);
  void DrainTrack(TrackState* track);
  void SignalEOSToTracks(status_t final_result);

  std::shared_ptr<MediaMeta> source_format_;
  std::map<unsigned, std::unique_ptr<TrackState>> tracks_by_id_;
  std::vector<unsigned> visible_track_ids_;
  std::map<unsigned, unsigned> stream_type_by_esid_;
  std::shared_ptr<media::Buffer> buffer_;
  off64_t offset_ = 0;
  status_t final_result_ = OK;
  bool scanning_ = true;
  bool initialized_ = false;
  bool eos_signaled_ = false;
  bool program_stream_map_valid_ = false;
};

}  // namespace player
}  // namespace ave

#endif  // DEMUXER_MPEG2_MPEG2_PS_DEMUXER_H_
