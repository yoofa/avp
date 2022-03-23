/*
 * ffmpeg_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_DECODER_H
#define FFMPEG_DECODER_H

#include <mutex>
#include <vector>

#include "base/logging.h"
#include "common/handler.h"
#include "common/media_defs.h"
#include "modules/ffmpeg/ffmpeg_helper.h"
#include "player/decoder.h"
#include "player/video_frame.h"

namespace avp {

class FFmpegDecoder : public Decoder, public Handler {
 public:
  FFmpegDecoder(bool audio, CodecType codecType);
  virtual ~FFmpegDecoder();

  virtual status_t setVideoSink(std::shared_ptr<VideoSink> videoSink) override;
  virtual status_t configure(std::shared_ptr<Message> format) override;
  virtual const char* name() override;

  status_t start() override;
  status_t stop() override;
  status_t flush() override;

  void setCallback(DecoderCallback* callback) override;

  status_t queueInputBuffer(std::shared_ptr<Buffer> buffer) override;
  status_t signalEndOfInputStream() override;

  status_t dequeueOutputBuffer(std::shared_ptr<Buffer>& buffer,
                               int32_t timeoutUs) override;
  status_t releaseOutputBuffer(std::shared_ptr<Buffer> buffer) override;

 protected:
  enum {
    kWhatQueueInputBuffer = '=qin',

    kWhatNotifyInputBufferAvailable = '=inA',
    kWhatNotifyOutputBufferAvailable = 'outA',
  };
  void onMessageReceived(const std::shared_ptr<Message>& message) override;

  void initLooper();

  void notifyInputBufferAvailable();
  void notifyOutputBufferAvailable();
  void notifyFormatChanged(std::shared_ptr<Message> format);
  void notifyError(status_t err);

  void Decode(std::shared_ptr<Buffer>& buffer);

  virtual status_t DecodeToBuffers(
      std::shared_ptr<Buffer>& in,
      std::vector<std::shared_ptr<Buffer>>& out) = 0;

  bool mAudio;
  CodecType mCodecType;
  std::shared_ptr<Looper> mLooper;
  DecoderCallback* mCallback;
  AVCodecContext* mCodecContext;
  AVFrame* mAvFrame;
  std::mutex mLock;
  std::vector<std::shared_ptr<Buffer>> mBufferQueue;
  size_t mInputPenddingCount;
};

} /* namespace avp */

#endif /* !FFMPEG_DECODER_H */
