/*
 * http_live_source.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_CONTENT_SOURCE_HTTP_LIVE_HTTP_LIVE_SOURCE_H_
#define AVP_CONTENT_SOURCE_HTTP_LIVE_HTTP_LIVE_SOURCE_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "api/content_source/content_source.h"
#include "base/net/http/http_provider.h"
#include "core/packet_source.h"
#include "media/foundation/media_meta.h"

#include "content_source/http_live/playlist_parser.h"

namespace ave {
class DataSource;
namespace media {
namespace mpeg2ts {
class PacketSource;
}  // namespace mpeg2ts
}  // namespace media
}

namespace ave {
namespace player {

class HttpLiveSource : public ContentSource {
 public:
  explicit HttpLiveSource(std::shared_ptr<net::HTTPProvider> http_provider);
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
  status_t DequeueAccessUnit(MediaType track_type,
                             std::shared_ptr<media::MediaFrame>& access_unit)
      override;
  std::shared_ptr<media::MediaMeta> GetFormat() override;
  status_t GetDuration(int64_t* duration_us) override;
  size_t GetTrackCount() const override;
  std::shared_ptr<media::MediaMeta> GetTrackInfo(size_t track_index) const override;
  std::shared_ptr<media::MediaMeta> GetTrackInfo(MediaType track_type) const override;
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
  };

  status_t PrepareLocked();
  status_t RefreshPlaylistLocked(bool initial);
  status_t LoadNextSegmentLocked();
  status_t LoadSegmentLocked(const http_live::MediaPlaylistSegment& segment,
                             int64_t segment_start_time_us);
  status_t LoadTsSegmentLocked(const std::shared_ptr<ave::DataSource>& data_source,
                               int64_t segment_start_time_us);
  status_t FetchUrlLocked(const std::string& url, std::vector<uint8_t>& data);
  status_t FetchTextLocked(const std::string& url, std::string& text);
  status_t QueueTsPacketsLocked(
      MediaType media_type,
      const std::shared_ptr<media::mpeg2ts::PacketSource>& source,
      int64_t segment_start_time_us);
  status_t EnsureTrackStateLocked(MediaType media_type,
                                  const std::shared_ptr<media::MediaMeta>& format);
  TrackState* FindTrackLocked(MediaType media_type);
  const TrackState* FindTrackLocked(MediaType media_type) const;
  status_t FinishPrepareLocked();
  void ResetLocked();
  static void OffsetFrameTimestamp(const std::shared_ptr<media::MediaFrame>& frame,
                                   int64_t offset_us);

  Notify* notify_ = nullptr;
  std::shared_ptr<net::HTTPProvider> http_provider_;

  std::string master_url_;
  std::string media_playlist_url_;
  std::unordered_map<std::string, std::string> headers_;
  http_live::Playlist playlist_;
  std::shared_ptr<media::MediaMeta> source_format_;
  std::vector<TrackState> tracks_;

  int64_t duration_us_ = -1;
  int32_t next_segment_sequence_ = 0;
  int64_t next_segment_start_time_us_ = 0;
  int64_t next_playlist_refresh_time_us_ = 0;
  bool prepared_ = false;
  bool started_ = false;
  bool end_of_stream_ = false;

  mutable std::mutex lock_;
};

}  // namespace player
}  // namespace ave

#endif  // AVP_CONTENT_SOURCE_HTTP_LIVE_HTTP_LIVE_SOURCE_H_
