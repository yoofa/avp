/*
 * mp4_demuxer.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_MP4_DEMUXER_H_
#define DEMUXER_MP4_DEMUXER_H_

#include <memory>
#include <mutex>
#include <vector>

#include "api/demuxer/demuxer.h"
#include "base/data_source/data_source.h"
#include "demuxer/isobmff/sample_table.h"
#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"
#include "media/foundation/media_source.h"

namespace ave {
namespace player {

using ave::media::MediaFrame;
using ave::media::MediaMeta;
using ave::media::MediaSource;

class Mp4Demuxer : public Demuxer {
 public:
  explicit Mp4Demuxer(std::shared_ptr<ave::DataSource> data_source);
  ~Mp4Demuxer() override;

  // Demuxer interface
  status_t GetFormat(std::shared_ptr<MediaMeta>& format) override;
  size_t GetTrackCount() override;
  status_t GetTrackFormat(std::shared_ptr<MediaMeta>& format,
                          size_t track_index) override;
  std::shared_ptr<MediaSource> GetTrack(size_t track_index) override;
  const char* name() override;

  // Called by factory after construction.
  status_t Init();

 private:
  friend struct Mp4Source;

  struct Track {
    std::shared_ptr<MediaMeta> meta;
    std::unique_ptr<isobmff::SampleTable> sample_table;
    uint32_t timescale = 0;
    int64_t duration_us = 0;
    uint32_t current_sample = 0;  // read cursor
    media::MediaType media_type = media::MediaType::UNKNOWN;
  };

  struct TrakParseContext {
    Track track;
    bool has_mdhd = false;
    bool has_hdlr = false;
    bool has_stbl = false;
  };

  // Box parsing
  status_t ParseBoxes(off64_t offset, off64_t end_offset, int depth);
  status_t ParseMoov(off64_t offset, off64_t size);
  status_t ParseTrak(off64_t offset, off64_t size);
  status_t ParseMdiaBox(off64_t offset, off64_t size, TrakParseContext* ctx);
  status_t ParseMdhd(off64_t offset, off64_t size, Track* track);
  status_t ParseHdlr(off64_t offset, off64_t size, Track* track);
  status_t ParseStbl(off64_t offset, off64_t size, Track* track);
  status_t ParseStsd(off64_t offset, off64_t size, Track* track);

  // Sample description parsing
  status_t ParseVideoSampleEntry(off64_t offset,
                                 off64_t size,
                                 uint32_t type,
                                 Track* track);
  status_t ParseAudioSampleEntry(off64_t offset,
                                 off64_t size,
                                 uint32_t type,
                                 Track* track);

  // Read a sample for a track
  status_t ReadSample(size_t track_index,
                      std::shared_ptr<MediaFrame>& frame,
                      const MediaSource::ReadOptions* options);

  std::shared_ptr<MediaMeta> source_format_;
  std::vector<Track> tracks_;
  std::mutex mutex_;
  bool initialized_ = false;
};

// MediaSource implementation for individual Mp4 tracks.
struct Mp4Source : public MediaSource {
  Mp4Source(Mp4Demuxer* demuxer,
            size_t track_index,
            std::shared_ptr<MediaMeta> format);
  ~Mp4Source() override = default;

  status_t Start(std::shared_ptr<ave::media::Message> params) override;
  status_t Stop() override;
  std::shared_ptr<MediaMeta> GetFormat() override;
  status_t Read(std::shared_ptr<MediaFrame>& frame,
                const ReadOptions* options) override;

 private:
  Mp4Demuxer* demuxer_;
  size_t track_index_;
  std::shared_ptr<MediaMeta> format_;
  bool started_ = false;
};

}  // namespace player
}  // namespace ave

#endif  // DEMUXER_MP4_DEMUXER_H_
