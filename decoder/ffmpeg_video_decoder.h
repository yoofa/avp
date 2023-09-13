/*
 * ffmpeg_video_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_VIDEO_DECODER_H
#define FFMPEG_VIDEO_DECODER_H

#include "decoder/ffmpeg_decoder.h"
#include "libavutil/rational.h"
#include "player/decoder.h"

namespace avp {

class FFmpegVideoDecoder : public FFmpegDecoder {
 public:
  FFmpegVideoDecoder(CodecType codecType);
  virtual ~FFmpegVideoDecoder();

  status_t setVideoSink(std::shared_ptr<VideoSink> videoSink) override;
  status_t configure(std::shared_ptr<Message> format) override;
  const char* name() override;

  virtual status_t DecodeToBuffers(
      std::shared_ptr<Buffer>& in,
      std::vector<std::shared_ptr<Buffer>>& out) override;
};

} /* namespace avp */
#endif /* !FFMPEG_VIDEO_DECODER_H */
