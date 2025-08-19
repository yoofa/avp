/*
 * VideoFileRender.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef VIDEOFILERENDER_H
#define VIDEOFILERENDER_H

#include "media/foundation/media_frame.h"
#include "media/foundation/media_sink_base.h"

namespace ave {
namespace media {

class VideoFileRender : public MediaFrameSink {
 public:
  VideoFileRender(const char* file);
  virtual ~VideoFileRender();
  void OnFrame(const std::shared_ptr<MediaFrame>& frame) override;

 private:
  int mFd;
};

}  // namespace media
}  // namespace ave

#endif /* !VIDEOFILERENDER_H */
