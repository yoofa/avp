/*
 * ffmpeg_demuxer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DEMUXER_H
#define FFMPEG_DEMUXER_H

#include "player/data_source.h"
#include "player/demuxer.h"

extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
#include "third_party/ffmpeg/libavformat/avio.h"
#include "third_party/ffmpeg/libavutil/avutil.h"
}

namespace avp {

struct FFmpegSource;

class FFmpegDemuxer : public Demuxer {
 public:
  explicit FFmpegDemuxer(std::shared_ptr<DataSource> dataSource);
  virtual ~FFmpegDemuxer();
  virtual size_t getTrackCount() override;
  virtual std::shared_ptr<MediaSource> getTrack(size_t trackIndex) override;
  virtual status_t getDemuxerMeta(std::shared_ptr<MetaData>& metaData) override;
  virtual status_t getTrackMeta(std::shared_ptr<MetaData>& metaData,
                                size_t trackIndex) override;

  virtual const char* name() override;

  status_t init();

 private:
  friend struct FFmpegSource;

  struct TrackInfo {
    TrackInfo(size_t index,
              std::shared_ptr<MetaData>,
              std::shared_ptr<FFmpegSource> source);
    ~TrackInfo();

    size_t mTrackIndex;
    std::shared_ptr<MetaData> mMeta;
    std::shared_ptr<FFmpegSource> mSource;
    std::list<std::shared_ptr<Buffer>> mPackets;

    size_t packetSize();
    status_t enqueuePacket(std::shared_ptr<Buffer> packet);
    status_t dequeuePacket(std::shared_ptr<Buffer>& packet);
  };

  status_t addTrack(const AVStream* avStream, size_t index);
  status_t readAnAvPacket(size_t index);
  status_t readAvFrame(std::shared_ptr<Buffer>& buffer,
                       size_t index,
                       const MediaSource::ReadOptions* options);

  std::shared_ptr<DataSource> mDataSource;
  AVFormatContext* mFormatContext;
  AVIOContext* mIOContext;
  std::shared_ptr<MetaData> mMeta;

  std::vector<TrackInfo> mTracks;
};

} /* namespace avp */

#endif /* !FFMPEG_DEMUXER_H */
