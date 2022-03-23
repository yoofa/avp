/*
 * VideoFileRender.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef VIDEOFILERENDER_H
#define VIDEOFILERENDER_H

#include "player/video_sink.h"

namespace avp {

class VideoFileRender : public VideoSink {
 public:
  VideoFileRender(const char* file);
  virtual ~VideoFileRender();
  virtual void onFrame(std::shared_ptr<Buffer>& frame) override;

 private:
  int mFd;
};

} /* namespace avp */

#endif /* !VIDEOFILERENDER_H */
