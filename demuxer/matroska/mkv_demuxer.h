/*
 * mkv_demuxer.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_MATROSKA_MKV_DEMUXER_H_
#define DEMUXER_MATROSKA_MKV_DEMUXER_H_

#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../api/demuxer/demuxer.h"
#include "base/data_source/data_source.h"
#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"
#include "media/foundation/media_source.h"

// Forward declarations for Ebml/Matroska elements
namespace libebml {
class IOCallback;
class EbmlStream;
class EbmlElement;
}  // namespace libebml

namespace libmatroska {
class KaxSegment;
class KaxInternalBlock;
class KaxCues;
class KaxCluster;
}  // namespace libmatroska

namespace ave {
namespace player {

using ave::base::DataSource;
using ave::media::MediaFrame;
using ave::media::MediaMeta;
using ave::media::MediaSource;
using ave::media::MediaType;

class MkvDemuxer;

struct MkvSource : public MediaSource {
  MkvSource(MkvDemuxer* demuxer,
            size_t track_index,
            std::shared_ptr<MediaMeta> format);
  ~MkvSource() override = default;

  status_t Start(std::shared_ptr<ave::media::Message> params) override;
  status_t Stop() override;
  std::shared_ptr<MediaMeta> GetFormat() override;
  status_t Read(std::shared_ptr<MediaFrame>& frame,
                const ReadOptions* options) override;

 private:
  MkvDemuxer* demuxer_;
  size_t track_index_;
  std::shared_ptr<MediaMeta> format_;
  bool started_ = false;
};

class MkvDemuxer : public Demuxer {
 public:
  explicit MkvDemuxer(std::shared_ptr<DataSource> data_source);
  ~MkvDemuxer() override;

  static std::optional<DemuxerSniffResult> Sniff(
      const std::shared_ptr<DataSource>& data_source);
  static std::vector<std::string> SupportedContainerMimeTypes();

  // Demuxer API
  status_t GetFormat(std::shared_ptr<MediaMeta>& format) override;
  size_t GetTrackCount() override;
  status_t GetTrackFormat(std::shared_ptr<MediaMeta>& format,
                          size_t track_index) override;
  std::shared_ptr<MediaSource> GetTrack(size_t track_index) override;
  const char* name() override;

  // Probes file and parses header elements. Called after construction.
  status_t Init();

 private:
  friend struct MkvSource;

  struct MkvTrack {
    uint32_t track_num = 0;
    size_t track_index = 0;
    std::shared_ptr<MediaMeta> meta;
    MediaType type = MediaType::UNKNOWN;
    std::list<std::shared_ptr<MediaFrame>> packet_queue;
    uint64_t default_duration_ns = 0;
    std::shared_ptr<MkvSource> source;
  };

  struct MkvCue {
    uint64_t time_us = 0;
    uint64_t file_pos = 0;
  };

  status_t ReadFrame(size_t track_index,
                     std::shared_ptr<MediaFrame>& frame,
                     const MediaSource::ReadOptions* options);

  status_t ReadNextBlock();
  void EnqueueFramesFromBlock(libmatroska::KaxInternalBlock& block,
                              uint64_t duration_scale_units = 0);
  void ParseCues(libmatroska::KaxCues* cues);
  MkvTrack* FindTrackByNumber(uint32_t track_num);

  std::shared_ptr<MediaMeta> source_format_;
  std::vector<MkvTrack> tracks_;
  std::vector<MkvCue> cues_;

  std::unique_ptr<libebml::IOCallback> io_callback_;
  std::unique_ptr<libebml::EbmlStream> stream_;
  libmatroska::KaxSegment* segment_ = nullptr;

  uint64_t timecode_scale_ = 1000000;  // default to 1ms in nanoseconds
  int64_t duration_us_ = -1;
  uint64_t first_cluster_offset_ = 0;

  // Sequential parsing state
  int upper_level_ = 0;
  libebml::EbmlElement* current_element_ = nullptr;
  libmatroska::KaxCluster* current_cluster_ = nullptr;

  std::mutex mutex_;
  const bool en_debug_log_;
};

}  // namespace player
}  // namespace ave

#endif  // DEMUXER_MATROSKA_MKV_DEMUXER_H_
