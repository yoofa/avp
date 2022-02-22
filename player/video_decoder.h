/*
 * video_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

namespace avp {

class VideoDecoder {
 protected:
  VideoDecoder() = delete;
  virtual ~VideoDecoder() = delete;
};

} /* namespace avp */

#endif /* !VIDEO_DECODER_H */
