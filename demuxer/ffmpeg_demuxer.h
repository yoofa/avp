/*
 * ffmpeg_demuxer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DEMUXER_H
#define FFMPEG_DEMUXER_H

#include "player/demuxer.h"

namespace avp {

class FFmpegDemuxer : public Demuxer {
 public:
  FFmpegDemuxer();
  virtual ~FFmpegDemuxer();
};

} /* namespace avp */

#endif /* !FFMPEG_DEMUXER_H */
