/*
 * ffmpeg_decoder.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_decoder.h"

namespace avp {

FFmpegDecoder::FFmpegDecoder() {}
FFmpegDecoder::~FFmpegDecoder() {}

status_t FFmpegDecoder::configure(std::shared_ptr<Message> format) {
  return OK;
}

const char* FFmpegDecoder::name() {
  return "";
}
status_t FFmpegDecoder::start() {
  return OK;
}
status_t FFmpegDecoder::stop() {
  return OK;
}
status_t FFmpegDecoder::flush() {
  return OK;
}

void FFmpegDecoder::setCallback(DecoderCallback* callback) {
  mCallback = callback;
}

status_t FFmpegDecoder::queueInputBuffer(std::shared_ptr<Buffer> buffer) {
  return OK;
}
status_t FFmpegDecoder::signalEndOfInputStream() {
  return OK;
}

status_t FFmpegDecoder::dequeueOutputBuffer(std::shared_ptr<Buffer> buffer) {
  return OK;
}
status_t FFmpegDecoder::releaseOutputBuffer(std::shared_ptr<Buffer> buffer) {
  return OK;
}
} /* namespace avp */
