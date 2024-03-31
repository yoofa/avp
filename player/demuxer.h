/*
 * demuxer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_H
#define DEMUXER_H

#include <memory>

#include "base/types.h"
#include "media/meta_data.h"
#include "player/media_source.h"

namespace avp {

class Demuxer {
 public:
  Demuxer() = default;
  virtual ~Demuxer() = default;

  virtual size_t getTrackCount() = 0;
  virtual std::shared_ptr<MediaSource> getTrack(size_t trackIndex) = 0;
  virtual status_t getDemuxerMeta(std::shared_ptr<MetaData>& metaData) = 0;
  virtual status_t getTrackMeta(std::shared_ptr<MetaData>& metaData,
                                size_t trackIndex) = 0;

  virtual const char* name() = 0;
};
} /* namespace avp */

#endif /* !DEMUXER_H */
