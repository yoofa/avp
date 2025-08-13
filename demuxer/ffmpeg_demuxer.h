/*
 * ffmpeg_demuxer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DEMUXER_H
#define FFMPEG_DEMUXER_H

#include <list>
#include <vector>
#include "api/demuxer/demuxer.h"
#include "base/data_source/data_source.h"
#include "media/foundation/media_source.h"

extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
#include "third_party/ffmpeg/libavformat/avio.h"
#include "third_party/ffmpeg/libavutil/avutil.h"
}

namespace ave {
namespace player {

using ave::media::MediaPacket;

struct FFmpegSource;

class FFmpegDemuxer : public Demuxer {
 public:
  explicit FFmpegDemuxer(std::shared_ptr<ave::DataSource> dataSource);
  ~FFmpegDemuxer() override;

  // Demuxer API
  status_t GetFormat(std::shared_ptr<ave::media::MediaMeta>& format) override;
  size_t GetTrackCount() override;
  status_t GetTrackFormat(std::shared_ptr<ave::media::MediaMeta>& format,
                          size_t trackIndex) override;
  std::shared_ptr<ave::media::MediaSource> GetTrack(size_t trackIndex) override;
  const char* name() override;

  // Internal init
  status_t Init();

 private:
  friend struct FFmpegSource;

  struct TrackInfo {
    TrackInfo(size_t index,
              std::shared_ptr<ave::media::MediaMeta>,
              std::shared_ptr<FFmpegSource> source);
    ~TrackInfo();

    size_t track_index;
    std::shared_ptr<ave::media::MediaMeta> meta;
    std::shared_ptr<FFmpegSource> source;
    std::list<std::shared_ptr<ave::media::MediaPacket>> packets;

    size_t PacketSize();
    status_t EnqueuePacket(std::shared_ptr<MediaPacket> packet);
    status_t DequeuePacket(std::shared_ptr<MediaPacket>& packet);
  };

  status_t AddTrack(const AVStream* avStream, size_t index);
  status_t ReadAnAvPacket(size_t index);
  status_t ReadAvFrame(std::shared_ptr<ave::media::MediaPacket>& packet,
                       size_t index,
                       const ave::media::MediaSource::ReadOptions* options);

  // std::shared_ptr<ave::DataSource> data_source_;
  AVFormatContext* av_format_context_;
  AVIOContext* av_io_context_;
  std::shared_ptr<ave::media::MediaMeta> source_format_;

  std::vector<TrackInfo> tracks_;
};

}  // namespace player
}  // namespace ave

#endif /* !FFMPEG_DEMUXER_H */
