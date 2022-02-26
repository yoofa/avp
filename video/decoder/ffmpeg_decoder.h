/*
 * ffmpeg_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DECODER_H
#define FFMPEG_DECODER_H

#include "player/decoder.h"

namespace avp {

class FFmpegDecoder : public Decoder {
 public:
  FFmpegDecoder();
  virtual ~FFmpegDecoder();

  virtual status_t configure(std::shared_ptr<Message> format);
  virtual const char* name();
  virtual status_t start();
  virtual status_t stop();
  virtual status_t flush();

  virtual void setCallback(DecoderCallback* callback);

  virtual status_t queueInputBuffer(std::shared_ptr<Buffer> buffer);
  virtual status_t signalEndOfInputStream();

  virtual status_t dequeueOutputBuffer(std::shared_ptr<Buffer> buffer);
  virtual status_t releaseOutputBuffer(std::shared_ptr<Buffer> buffer);

 private:
  DecoderCallback* mCallback;
};

} /* namespace avp */

#endif /* !FFMPEG_DECODER_H */
