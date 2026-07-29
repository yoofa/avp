/*
 * http_live_source.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_CONTENT_SOURCE_HTTP_LIVE_HTTP_LIVE_SOURCE_H_
#define AVP_CONTENT_SOURCE_HTTP_LIVE_HTTP_LIVE_SOURCE_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "base/data_source/data_source.h"
#include "base/net/http/http_provider.h"
#include "media/foundation/media_meta.h"

#include "../../api/content_source/content_source.h"
#include "../../content_source/http_live/playlist_parser.h"
#include "../../core/packet_source.h"
#include "../../demuxer/isobmff/fragmented_mp4_parser.h"

namespace ave {
namespace media {
namespace mpeg2ts {
class PacketSource;
}  // namespace mpeg2ts
}  // namespace media
}  // namespace ave

namespace ave {
namespace player {

using ave::base::DataSource;
using ave::base::net::HTTPProvider;

class HttpLiveSource : public ContentSource {
 public:
  explicit HttpLiveSource(std::shared_ptr<HTTPProvider> http_provider);
  ~HttpLiveSource() override;

  status_t SetDataSource(
      const char* url,
      const std::unordered_map<std::string, std::string>& headers);

  void SetNotify(Notify* notify) override;
  void Prepare() override;
  void Start() override;
  void Stop() override;
  void Pause() override;
  void Resume() override;
  status_t DequeueAccessUnit(
      MediaType track_type,
      std::shared_ptr<media::MediaFrame>& access_unit) override;
  std::shared_ptr<media::MediaMeta> GetFormat() override;
  status_t GetDuration(int64_t* duration_us) override;
  size_t GetTrackCount() const override;
  std::shared_ptr<media::MediaMeta> GetTrackInfo(
      size_t track_index) const override;
  std::shared_ptr<media::MediaMeta> GetTrackInfo(
      MediaType track_type) const override;
  status_t SelectTrack(size_t track_index, bool select) override;
  status_t SeekTo(int64_t seek_time_us, SeekMode mode) override;
  bool IsStreaming() const override;
  status_t FeedMoreESData() override;

 private:
  struct TrackState {
    size_t index = 0;
    MediaType media_type = MediaType::UNKNOWN;
    std::shared_ptr<media::MediaMeta> format;
    std::shared_ptr<PacketSource> packet_source;
    int64_t next_timestamp_us = -1;
    int64_t timestamp_segment_start_us = -1;
    int64_t timestamp_offset_us = 0;
  };

  struct StreamState {
    std::string playlist_url;
    http_live::Playlist playlist;
    std::vector<uint8_t> init_segment_bytes;
    std::vector<isobmff::FragmentedMp4Track> fragmented_mp4_tracks;
    std::string init_segment_cache_key;
    int32_t next_segment_sequence = 0;
    int64_t next_segment_start_time_us = 0;
    int64_t next_playlist_refresh_time_us = 0;

    bool active() const { return !playlist_url.empty(); }
    bool is_live() const { return active() && playlist.is_live; }
  };

  status_t PrepareLocked();
  status_t RefreshPlaylistLocked(bool initial);
  status_t RefreshStreamPlaylistLocked(StreamState& stream, bool initial);
  status_t LoadNextSegmentLocked();
  status_t LoadNextSegmentLocked(StreamState& stream);
  status_t LoadSegmentLocked(StreamState& stream,
                             const http_live::MediaPlaylistSegment& segment,
                             int64_t segment_start_time_us);
  status_t LoadTsSegmentLocked(const std::shared_ptr<DataSource>& data_source,
                               int64_t segment_start_time_us);
  status_t LoadFragmentedSegmentLocked(
      StreamState& stream,
      const http_live::MediaPlaylistSegment& segment,
      int64_t segment_start_time_us);
  status_t FetchUrlLocked(const std::string& url, std::vector<uint8_t>& data);
  status_t FetchUrlRangeLocked(const std::string& url,
                               off64_t offset,
                               int64_t length,
                               std::vector<uint8_t>& data);
  status_t FetchTextLocked(const std::string& url, std::string& text);
  status_t QueueTsPacketsLocked(
      MediaType media_type,
      const std::shared_ptr<media::mpeg2ts::PacketSource>& source,
      int64_t segment_start_time_us);
  void NormalizeTsFrameTimestampLocked(
      TrackState* track,
      const std::shared_ptr<media::MediaFrame>& frame,
      int64_t segment_start_time_us);
  status_t QueueFragmentedMp4SamplesLocked(
      const std::vector<isobmff::FragmentedMp4Track>& tracks,
      std::vector<std::shared_ptr<media::MediaFrame>> samples,
      int64_t segment_start_time_us);
  status_t EnsureTrackStateLocked(
      MediaType media_type,
      const std::shared_ptr<media::MediaMeta>& format);
  TrackState* FindTrackLocked(MediaType media_type);
  const TrackState* FindTrackLocked(MediaType media_type) const;
  bool HasSufficientBufferedDataLocked() const;
  status_t LoadMoreLocked();
  void LoadMoreInBackground();
  void PublishTrackPacketSourceLocked(MediaType media_type,
                                      std::shared_ptr<PacketSource> source);
  status_t FinishPrepareLocked();
  void ResetLocked();
  static void OffsetFrameTimestamp(
      const std::shared_ptr<media::MediaFrame>& frame,
      int64_t offset_us);

  Notify* notify_ = nullptr;
  std::shared_ptr<HTTPProvider> http_provider_;

  std::string master_url_;
  std::unordered_map<std::string, std::string> headers_;
  std::shared_ptr<media::MediaMeta> source_format_;
  std::vector<TrackState> tracks_;
  StreamState primary_stream_;
  StreamState audio_stream_;
  http_live::Playlist playlist_;

  int64_t duration_us_ = -1;
  bool prepared_ = false;
  bool started_ = false;
  bool end_of_stream_ = false;

  // Segment downloads and TS parsing can take hundreds of milliseconds. The
  // decoder hot path must not wait for lock_ while that work is in progress:
  // PacketSource itself is thread-safe and its lifetime is published here.
  std::shared_ptr<PacketSource> audio_packet_source_;
  std::shared_ptr<PacketSource> video_packet_source_;
  std::atomic_bool dequeue_ready_ = false;
  std::atomic_bool dequeue_eos_ = false;

  mutable std::mutex lock_;
  std::jthread segment_loader_;
  std::atomic_bool segment_load_in_progress_ = false;
};

}  // namespace player
}  // namespace ave

#endif  // AVP_CONTENT_SOURCE_HTTP_LIVE_HTTP_LIVE_SOURCE_H_
