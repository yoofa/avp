/*
 * demuxer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_H
#define DEMUXER_H

#include <memory>

#include "api/player_interface.h"
#include "base/data_source/data_source.h"
#include "media/foundation/media_format.h"
#include "media/foundation/media_source.h"

namespace ave {
namespace player {

using ave::media::MediaFormat;
using ave::media::MediaSource;

class Demuxer {
 public:
  explicit Demuxer(std::shared_ptr<ave::DataSource>(data_source))
      : data_source_(std::move(data_source)) {}
  virtual ~Demuxer() = default;

  virtual status_t GetFormat(std::shared_ptr<MediaFormat>& format) = 0;

  virtual size_t GetTrackCount() = 0;

  virtual status_t GetTrackFormat(std::shared_ptr<MediaFormat>& format,
                                  size_t trackIndex) = 0;

  virtual std::shared_ptr<MediaSource> GetTrack(size_t trackIndex) = 0;

  virtual const char* name() = 0;

 protected:
  std::shared_ptr<ave::DataSource> data_source_;
};

}  // namespace player
}  // namespace ave

#endif /* !DEMUXER_H */
