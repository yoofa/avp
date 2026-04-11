/*
 * mpeg2_common.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_MPEG2_MPEG2_COMMON_H_
#define DEMUXER_MPEG2_MPEG2_COMMON_H_

#include <functional>
#include <memory>

#include "base/data_source/data_source.h"
#include "media/foundation/media_source.h"
#include "media/modules/mpeg2ts/packet_source.h"

namespace ave {
namespace player {
namespace mpeg2 {

struct TsPacketLayout {
  size_t sync_offset = 0;
  size_t packet_stride = 188;
};

class PacketSourceTrack : public media::MediaSource {
 public:
  using EnsureDataFn = std::function<status_t()>;

  PacketSourceTrack(std::shared_ptr<media::mpeg2ts::PacketSource> source,
                    EnsureDataFn ensure_data_fn);
  ~PacketSourceTrack() override = default;

  status_t Start(std::shared_ptr<media::Message> params) override;
  status_t Stop() override;
  std::shared_ptr<media::MediaMeta> GetFormat() override;
  status_t Read(std::shared_ptr<media::MediaFrame>& frame,
                const ReadOptions* options) override;

 private:
  std::shared_ptr<media::mpeg2ts::PacketSource> source_;
  EnsureDataFn ensure_data_fn_;
  bool started_ = false;
  bool waiting_for_video_sync_ = false;
};

bool SniffMpeg2Ts(std::shared_ptr<ave::DataSource> data_source,
                  TsPacketLayout* layout);
bool SniffMpeg2Ps(std::shared_ptr<ave::DataSource> data_source);

}  // namespace mpeg2
}  // namespace player
}  // namespace ave

#endif  // DEMUXER_MPEG2_MPEG2_COMMON_H_
