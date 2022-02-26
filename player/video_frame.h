/*
 * video_frame.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef VIDEO_FRAME_H
#define VIDEO_FRAME_H

#include "common/message.h"

namespace avp {
class VideoFrame : public MessageObject {
 public:
  VideoFrame();
  virtual ~VideoFrame();
};

} /* namespace avp */

#endif /* !VIDEO_FRAME_H */
