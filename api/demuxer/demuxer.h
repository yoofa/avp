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
#include "media/foundation/media_format.h"
#include "media/foundation/media_source.h"

namespace avp {

using ave::media::MediaFormat;
using ave::media::MediaSource;

class Demuxer {
 public:
  Demuxer() = default;
  virtual ~Demuxer() = default;

  virtual status_t GetFormat(std::shared_ptr<MediaFormat>& format) = 0;

  virtual size_t GetTrackCount() = 0;

  virtual status_t GetTrackFormat(std::shared_ptr<MediaFormat>& format,
                                  size_t trackIndex) = 0;

  virtual std::shared_ptr<MediaSource> GetTrack(size_t trackIndex) = 0;

  virtual const char* name() = 0;
};
} /* namespace avp */

#endif /* !DEMUXER_H */
