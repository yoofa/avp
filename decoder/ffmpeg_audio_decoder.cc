/*
 * ffmpeg_audio_decoder.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_audio_decoder.h"

#include "base/checks.h"
#include "base/logging.h"
#include "media/media_errors.h"

namespace avp {

FFmpegAudioDecoder::FFmpegAudioDecoder(CodecType codecType)
    : FFmpegDecoder(true, codecType) {}
FFmpegAudioDecoder::~FFmpegAudioDecoder() {}

status_t FFmpegAudioDecoder::configure(std::shared_ptr<Message> format) {
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

  AudioFormatToAVCodecContext(format, mCodecContext);

  mCodecContext->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
  mCodecContext->err_recognition = AV_EF_CAREFUL;
  mCodecContext->thread_count = 2;
  mCodecContext->opaque = this;
  //    mCodecContext->flags |= CODEC_FLAG_EMU_EDGE;
  //    mCodecContext->get_buffer = GetVideoBufferImpl;
  //    mCodecContext->release_buffer = ReleaseVideoBufferImpl;

  const AVCodec* codec = avcodec_find_decoder(mCodecContext->codec_id);
  if (!codec) {
    LOG(LS_INFO) << "unsupported";
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

const char* FFmpegAudioDecoder::name() {
  return "FFmpeg-Audio-Decoder";
}

status_t FFmpegAudioDecoder::DecodeToBuffers(
    std::shared_ptr<Buffer>& in,
    std::vector<std::shared_ptr<Buffer>>& out) {
  int64_t timeUs;

  CHECK(in->meta()->findInt64("timeUs", &timeUs));

  AVPacket packet;
  packet.buf = nullptr;
  packet.side_data = nullptr;
  packet.data = in->data();
  packet.size = in->size();
  // hexdump(in->data(), 100);
  // av_packet_unref(&packet);

  // av_frame_unref(mAvFrame);

  int ret = avcodec_send_packet(mCodecContext, &packet);

  if (ret < 0) {
    return ret;
  }

  while (ret >= 0) {
    ret = avcodec_receive_frame(mCodecContext, mAvFrame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      // TODO(youfa) eos
      return OK;
    } else if (ret < 0) {
      return ret;
    }

    int dataSize = av_get_bytes_per_sample(mCodecContext->sample_fmt);
    if (dataSize < 0) {
      return -1;
    }
    auto frame =
        createAudioBufferFromAvFrame(mAvFrame, mCodecContext->time_base);
    if (frame != nullptr) {
      out.push_back(frame);
    }
  }

  return OK;
}

} /* namespace avp */
