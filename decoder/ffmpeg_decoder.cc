/*
 * ffmpeg_decoder.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "ffmpeg_decoder.h"

#include "base/checks.h"
#include "base/hexdump.h"
#include "base/logging.h"
#include "media/media_errors.h"
#include "media/utils.h"
#include "modules/ffmpeg/ffmpeg_helper.h"

namespace avp {

FFmpegDecoder::FFmpegDecoder(bool audio, CodecType codecType)
    : mAudio(audio),
      mCodecType(codecType),
      mLooper(std::make_shared<Looper>()),
      mCodecContext(avcodec_alloc_context3(nullptr)),
      mAvFrame(nullptr),
      time_base_({1, 1000000}),
      mInputPenddingCount(0) {
  mLooper->setName("FFmpegDecoder");
  LOG(LS_INFO) << "is audio:" << mAudio << ", type:" << mCodecType;
}

FFmpegDecoder::~FFmpegDecoder() {
  if (mCodecContext) {
    av_free(mCodecContext->extradata);
    avcodec_close(mCodecContext);
    av_free(mCodecContext);
    mCodecContext = NULL;
  }
  if (mAvFrame) {
    av_free(mAvFrame);
    mAvFrame = NULL;
  }
}

void FFmpegDecoder::initLooper() {
  mLooper->registerHandler(shared_from_this());
  mLooper->start();
}

status_t FFmpegDecoder::setVideoSink(std::shared_ptr<VideoSink> videoSink) {
  return OK;
}

status_t FFmpegDecoder::configure(std::shared_ptr<Message> format) {
  return OK;
}

const char* FFmpegDecoder::name() {
  return "FFmpegDecoder";
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
  std::lock_guard<std::mutex> lock(mLock);
  mCallback = callback;
}

status_t FFmpegDecoder::queueInputBuffer(std::shared_ptr<Buffer> buffer) {
  // int64_t timeUs;
  // CHECK(buffer->meta()->findInt64("timeUs", &timeUs));
  // LOG(LS_INFO) << "queueInputBuffer[" << (mAudio ? "a" : "v")
  //             << "], pts:" << timeUs;
  std::lock_guard<std::mutex> lock(mLock);
  if (mInputPenddingCount > 5) {
    return ERROR_RETRY;
  }
  mInputPenddingCount++;
  auto msg =
      std::make_shared<Message>(kWhatQueueInputBuffer, shared_from_this());
  msg->setBuffer("buffer", std::move(buffer));
  msg->post();
  return OK;
}

status_t FFmpegDecoder::signalEndOfInputStream() {
  return OK;
}

status_t FFmpegDecoder::dequeueOutputBuffer(std::shared_ptr<Buffer>& buffer,
                                            int32_t timeoutUs) {
  std::lock_guard<std::mutex> lock(mLock);
  auto front = mBufferQueue.begin();
  if (front != mBufferQueue.end()) {
    buffer = std::move(*front);
    mBufferQueue.erase(mBufferQueue.begin());
    return OK;
  }
  return TIMED_OUT;
}

status_t FFmpegDecoder::releaseOutputBuffer(std::shared_ptr<Buffer> buffer) {
  return OK;
}

void FFmpegDecoder::Decode(std::shared_ptr<Buffer>& buffer) {
  {
    std::lock_guard<std::mutex> lock(mLock);
    CHECK(mInputPenddingCount > 0);
    mInputPenddingCount--;
    if (mInputPenddingCount < 2) {
      auto msg = std::make_shared<Message>(kWhatNotifyInputBufferAvailable,
                                           shared_from_this());
      msg->post();
    }
  }

  std::vector<std::shared_ptr<Buffer>> frames;

  status_t err = DecodeToBuffers(buffer, frames);
  if (err < 0) {
    LOG(LS_ERROR) << "decoing err:" << err;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mLock);
    size_t count = frames.size();
    // LOG(LS_INFO) << "decoding cout:" << count;

    for (size_t i = 0; i < count; i++) {
      auto frame = frames[i];
      if (frame.get()) {
        mBufferQueue.push_back(std::move(frame));
      }
      auto msg = std::make_shared<Message>(kWhatNotifyOutputBufferAvailable,
                                           shared_from_this());
      msg->post();
    }
  }
}

// status_t FFmpegDecoder::DecodeToBuffer(std::shared_ptr<Buffer>& in,
//                                       std::shared_ptr<Buffer>& out) {
//  int64_t timeUs;
//
//  CHECK(in->meta()->findInt64("timeUs", &timeUs));
//  std::shared_ptr<Buffer> nalBuffer = std::make_shared<Buffer>(in->size() +
//  4); memcpy(nalBuffer->data(), "\x00\x00\x00\x01", 4);
//  memcpy(nalBuffer->data() + 4, in->data(), in->size());
//
//  AVPacket packet;
//  packet.buf = nullptr;
//  packet.side_data = nullptr;
//  packet.data = in->data();
//  packet.size = in->size();
//  // hexdump(nalBuffer->data(), 100);
//  // av_packet_unref(&packet);
//
//  // av_frame_unref(mAvFrame);
//
//  int gotFrame = 0;
//  int ret = avcodec_send_packet(mCodecContext, &packet);
//
//  if (ret < 0) {
//    return ret;
//  }
//
//  ret = avcodec_receive_frame(mCodecContext, mAvFrame);
//  if (ret >= 0) {
//    gotFrame = 1;
//
//  } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
//    ret = 0;
//  }
//
//  if (ret < 0) {
//    LOG(LS_INFO) << "error decoding a video frame with timestamp " << timeUs;
//  }
//
//  if (!gotFrame) {
//    return ret;
//  }
//
//  if (!mAvFrame->data[0] || !mAvFrame->data[1] || !mAvFrame->data[2]) {
//    LOG(LS_ERROR)
//        << "Video frame was produced yet has invalid frame data. and err:"
//        << ret;
//    return ret;
//  }
//
//  // if (!mAvFrame->opaque) {
//  //  LOG(LS_ERROR) << "VideoFrame object associated with frame data not
//  set.";
//  //  return ret;
//  //}
//  // LOG(LS_INFO) << "#################### got frame, res: [" <<
//  mAvFrame->width
//  //             << "x" << mAvFrame->height << "], pts:" << mAvFrame->pts
//  //             << ", size: " << mAvFrame->pkt_size;
//  // out = Buffer::CreateAsCopy(mAvFrame->data, sizeof(mAvFrame->data));
//  out = createBufferFromAvFrame(mAvFrame);
//
//  out->meta()->setInt64("timeUs", mAvFrame->pts);
//  out->meta()->setInt32("width", mAvFrame->width);
//  out->meta()->setInt32("height", mAvFrame->height);
//  out->meta()->setInt32("stride", mAvFrame->linesize[0]);
//  return OK;
//}
void FFmpegDecoder::notifyInputBufferAvailable() {
  if (mCallback) {
    mCallback->onInputBufferAvailable();
  }
}
void FFmpegDecoder::notifyOutputBufferAvailable() {
  if (mCallback) {
    mCallback->onOutputBufferAvailable();
  }
}
void FFmpegDecoder::notifyFormatChanged(std::shared_ptr<Message> format) {
  if (mCallback) {
    mCallback->onFormatChange(format);
  }
}
void FFmpegDecoder::notifyError(status_t err) {
  if (mCallback) {
    mCallback->onError(err);
  }
}

void FFmpegDecoder::onMessageReceived(const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatQueueInputBuffer: {
      std::shared_ptr<Buffer> buffer;
      CHECK(msg->findBuffer("buffer", buffer));
      Decode(buffer);
      break;
    }
    case kWhatNotifyOutputBufferAvailable: {
      notifyOutputBufferAvailable();
      break;
    }
    case kWhatNotifyInputBufferAvailable: {
      notifyInputBufferAvailable();
      break;
    }
    default:
      break;
  }
}
} /* namespace avp */
