/*
 * decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DECODER_H
#define DECODER_H

#include <memory>

#include "base/errors.h"
#include "media/message.h"
#include "player/video_sink.h"

namespace avp {
class Decoder {
 public:
  class DecoderCallback {
   public:
    enum {
      kWhatResolutionChange = 'resC',
      kWhatColorSpaceChange = 'colo',
    };

    DecoderCallback() = default;
    virtual ~DecoderCallback() = default;

    virtual void onInputBufferAvailable() = 0;
    virtual void onOutputBufferAvailable() = 0;
    virtual void onFormatChange(std::shared_ptr<Message> format) = 0;
    virtual void onError(status_t err) = 0;
  };

  Decoder() = default;
  virtual ~Decoder() = default;

  virtual status_t setVideoSink(std::shared_ptr<VideoSink> videoSink) = 0;
  virtual status_t configure(std::shared_ptr<Message> format) = 0;
  virtual const char* name() = 0;
  virtual status_t start() = 0;
  virtual status_t stop() = 0;
  virtual status_t flush() = 0;

  virtual void setCallback(DecoderCallback* callback) = 0;

  virtual status_t queueInputBuffer(std::shared_ptr<Buffer> buffer) = 0;
  virtual status_t signalEndOfInputStream() = 0;

  virtual status_t dequeueOutputBuffer(std::shared_ptr<Buffer>& buffer,
                                       int32_t timeoutUs) = 0;
  virtual status_t releaseOutputBuffer(std::shared_ptr<Buffer> buffer) = 0;
};
} /* namespace avp */

#endif /* !DECODER_H */
