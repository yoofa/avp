/*
 * video_sink.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef VIDEO_SINK_H
#define VIDEO_SINK_H

#include <memory>

#include "media/message.h"
#include "player/video_frame.h"

namespace avp {

using ave::Buffer;

class VideoSink : public MessageObject {
 public:
  VideoSink() = default;
  virtual ~VideoSink() = default;
  virtual void onFrame(std::shared_ptr<Buffer>& frame) = 0;
  virtual int64_t render_latency() { return 0; }
};
} /* namespace avp */

#endif /* !VIDEO_SINK_H */
