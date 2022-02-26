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
#include "common/message.h"

namespace avp {
class Decoder {
 public:
  struct DecoderCallback {
    void onOutputBufferAvailable();
    void onFormatChange(std::shared_ptr<Message> format);
    void onError(status_t err);
  };
  Decoder() = default;
  virtual ~Decoder() = default;

  virtual status_t configure(std::shared_ptr<Message> format) = 0;
  virtual const char* name() = 0;
  virtual status_t start() = 0;
  virtual status_t stop() = 0;
  virtual status_t flush() = 0;

  virtual void setCallback(DecoderCallback* callback) = 0;

  virtual status_t queueInputBuffer(std::shared_ptr<Buffer> buffer) = 0;
  virtual status_t signalEndOfInputStream() = 0;

  virtual status_t dequeueOutputBuffer(std::shared_ptr<Buffer> buffer) = 0;
  virtual status_t releaseOutputBuffer(std::shared_ptr<Buffer> buffer) = 0;
};
} /* namespace avp */

#endif /* !DECODER_H */
