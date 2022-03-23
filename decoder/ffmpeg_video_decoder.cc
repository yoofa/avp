/*
 * ffmpeg_video_decoder.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_video_decoder.h"

#include "base/checks.h"
#include "base/logging.h"
#include "common/media_errors.h"

namespace avp {

FFmpegVideoDecoder::FFmpegVideoDecoder(CodecType codecType)
    : FFmpegDecoder(false, codecType) {}

FFmpegVideoDecoder::~FFmpegVideoDecoder() {}

status_t FFmpegVideoDecoder::setVideoSink(
    std::shared_ptr<VideoSink> videoSink) {
  return OK;
}

status_t FFmpegVideoDecoder::configure(std::shared_ptr<Message> format) {
  initLooper();

#if 1
  av_log_set_level(AV_LOG_INFO);
  // av_log_set_callback(ffmpeg_log_default);
#endif

#if 0
  void* iterate_data = nullptr;
  const AVCodec* tCodec = av_codec_iterate(&iterate_data);
  while (tCodec != nullptr) {
    LOG(LS_INFO) << "##### tCodec.name:" << tCodec->name;
    tCodec = av_codec_iterate(&iterate_data);
  }
#endif

  VideoFormatToAVCodecContext(format, mCodecContext);

  mCodecContext->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
  mCodecContext->err_recognition = AV_EF_CAREFUL;
  mCodecContext->thread_count = 2;
  mCodecContext->opaque = this;
  //    mCodecContext->flags |= CODEC_FLAG_EMU_EDGE;
  //    mCodecContext->get_buffer = GetVideoBufferImpl;
  //    mCodecContext->release_buffer = ReleaseVideoBufferImpl;

  const AVCodec* codec = avcodec_find_decoder(mCodecContext->codec_id);
  if (!codec) {
    return ERROR_UNSUPPORTED;
  }

  int32_t err = avcodec_open2(mCodecContext, codec, nullptr);
  if (err < 0) {
    LOG(LS_ERROR) << "avcodec open failed :" << err;
    return UNKNOWN_ERROR;
  }
  avcodec_flush_buffers(mCodecContext);

  mAvFrame = av_frame_alloc();

  LOG(LS_INFO) << "codec open success";

  return OK;
}

const char* FFmpegVideoDecoder::name() {
  return "FFmpeg-Video-Decoder";
}

status_t FFmpegVideoDecoder::DecodeToBuffers(
    std::shared_ptr<Buffer>& in,
    std::vector<std::shared_ptr<Buffer>>& out) {
  int64_t timeUs;

  CHECK(in->meta()->findInt64("timeUs", &timeUs));
  std::shared_ptr<Buffer> nalBuffer = std::make_shared<Buffer>(in->size() + 4);
  memcpy(nalBuffer->data(), "\x00\x00\x00\x01", 4);
  memcpy(nalBuffer->data() + 4, in->data(), in->size());

  AVPacket packet;
  packet.buf = nullptr;
  packet.side_data = nullptr;
  packet.data = in->data();
  packet.size = in->size();
  // hexdump(nalBuffer->data(), 100);
  // av_packet_unref(&packet);

  // av_frame_unref(mAvFrame);

  int gotFrame = 0;
  int ret = avcodec_send_packet(mCodecContext, &packet);

  if (ret < 0) {
    return ret;
  }

  while (ret >= 0) {
    ret = avcodec_receive_frame(mCodecContext, mAvFrame);
    if (ret >= 0) {
      gotFrame = 1;
    } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      // TODO(youfa) eos
      ret = OK;
      break;
    }

    if (ret < 0) {
      LOG(LS_INFO) << "error decoding a video frame with timestamp " << timeUs;
      continue;
    }

    if (!mAvFrame->data[0] || !mAvFrame->data[1] || !mAvFrame->data[2]) {
      LOG(LS_ERROR)
          << "Video frame was produced yet has invalid frame data. and err:"
          << ret;
      continue;
    }

    // if (!mAvFrame->opaque) {
    //  LOG(LS_ERROR) << "VideoFrame object associated with frame data not
    //  set."; return ret;
    //}
    // LOG(LS_INFO) << "#################### got frame, res: [" <<
    // mAvFrame->width
    //             << "x" << mAvFrame->height << "], pts:" << mAvFrame->pts
    //             << ", size: " << mAvFrame->pkt_size;
    auto frame = createVideoBufferFromAvFrame(mAvFrame);
    if (frame != nullptr) {
      out.push_back(frame);
    }
  }
  return ret;
}

} /* namespace avp */
