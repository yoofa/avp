/*
 * ffmpeg_helper.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "ffmpeg_helper.h"

#include "base/checks.h"
#include "base/hexdump.h"
#include "base/logging.h"

#include "media/channel_layout.h"
#include "media/codec_constants.h"
#include "media/media_defs.h"
#include "media/meta_data.h"
#include "media/meta_data_utils.h"

#include "libavutil/rational.h"

namespace avp {

static const AVRational kMicrosBase = {1, 1000000};

int64_t ConvertFromTimeBase(const AVRational& time_base, int64_t pkt_pts) {
  return av_rescale_q(pkt_pts, time_base, kMicrosBase);
}

int64_t ConvertToTimeBase(const AVRational& time_base, const int64_t time_us) {
  return av_rescale_q(time_us, kMicrosBase, time_base);
}

void ffmpeg_log_default(void* p_unused,
                        int i_level,
                        const char* psz_fmt,
                        va_list arg) {
  char buf[4096];
  int length = vsnprintf(buf, sizeof(buf), psz_fmt, arg);
  if (length) {
    LOG(avp::LS_ERROR) << "ffmpeg log:" << buf;
  }
}

const char* VideoCodecId2Mime(AVCodecID codecId) {
  switch (codecId) {
    case AV_CODEC_ID_VP8:
      return MEDIA_MIMETYPE_VIDEO_VP8;
    case AV_CODEC_ID_VP9:

      return MEDIA_MIMETYPE_VIDEO_VP9;
    case AV_CODEC_ID_AV1:
      return MEDIA_MIMETYPE_VIDEO_AV1;
    case AV_CODEC_ID_H264:
      return MEDIA_MIMETYPE_VIDEO_AVC;
    case AV_CODEC_ID_HEVC:
      return MEDIA_MIMETYPE_VIDEO_HEVC;
    case AV_CODEC_ID_MPEG4:
      return MEDIA_MIMETYPE_VIDEO_MPEG4;
    case AV_CODEC_ID_H263:
      return MEDIA_MIMETYPE_VIDEO_H263;
    case AV_CODEC_ID_MPEG2VIDEO:
      return MEDIA_MIMETYPE_VIDEO_MPEG2;
    case AV_CODEC_ID_RAWVIDEO:
      return MEDIA_MIMETYPE_VIDEO_RAW;
    case AV_CODEC_ID_DVVIDEO:
      return MEDIA_MIMETYPE_VIDEO_DOLBY_VISION;
      //    case AV_CODEC_ID_SCRA:
      //      return MEDIA_MIMETYPE_VIDEO_SCRAMBLED;
      //    case AV_CODEC_ID_AV1:
      //      return MEDIA_MIMETYPE_VIDEO_DIVX;
      //    case AV_CODEC_ID_AV1:
      //      return MEDIA_MIMETYPE_VIDEO_DIVX3;
      //    case AV_CODEC_ID_:
      //      return MEDIA_MIMETYPE_VIDEO_XVID;
    case AV_CODEC_ID_MJPEG:
      return MEDIA_MIMETYPE_VIDEO_MJPEG;

    default:
      return "not support";
  }
}

const char* AudioCodecId2Mime(AVCodecID codecId) {
  switch (codecId) {
    case AV_CODEC_ID_AMR_NB:
      return MEDIA_MIMETYPE_AUDIO_AMR_NB;
    case AV_CODEC_ID_AMR_WB:
      return MEDIA_MIMETYPE_AUDIO_AMR_WB;
      //    case AV_CODEC_ID_:
      //   return  MEDIA_MIMETYPE_AUDIO_MPEG;  // layer III
      //    case AV_CODEC_ID_AMR_NB:
      //   return  MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_I;
      //    case AV_CODEC_ID_AMR_NB:
      //   return  MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II;
      //    case AV_CODEC_ID_MIDI:
      //   return  MEDIA_MIMETYPE_AUDIO_MIDI;
    case AV_CODEC_ID_AAC:
      return MEDIA_MIMETYPE_AUDIO_AAC;
      //    case AV_CODEC_ID_AACAD:
      //      return MEDIA_MIMETYPE_AUDIO_AAC_ADTS;
    case AV_CODEC_ID_QCELP:
      return MEDIA_MIMETYPE_AUDIO_QCELP;
    case AV_CODEC_ID_VORBIS:
      return MEDIA_MIMETYPE_AUDIO_VORBIS;
    case AV_CODEC_ID_OPUS:
      return MEDIA_MIMETYPE_AUDIO_OPUS;
      //    case AV_CODEC_ID_G7:
      //      return MEDIA_MIMETYPE_AUDIO_G711_ALAW;
      //    case AV_CODEC_ID_AMR_NB:
      //      return MEDIA_MIMETYPE_AUDIO_G711_MLAW;
      //    case AV_CODEC_ID_AUDIO:
      //      return MEDIA_MIMETYPE_AUDIO_RAW;
    case AV_CODEC_ID_FLAC:
      return MEDIA_MIMETYPE_AUDIO_FLAC;
    case AV_CODEC_ID_GSM_MS:
      return MEDIA_MIMETYPE_AUDIO_MSGSM;
    case AV_CODEC_ID_AC3:
      return MEDIA_MIMETYPE_AUDIO_AC3;
    case AV_CODEC_ID_EAC3:
      return MEDIA_MIMETYPE_AUDIO_EAC3;
    case AV_CODEC_ID_ALAC:
      return MEDIA_MIMETYPE_AUDIO_ALAC;
    case AV_CODEC_ID_WMAV1:
    case AV_CODEC_ID_WMAV2:
      return MEDIA_MIMETYPE_AUDIO_WMA;
    case AV_CODEC_ID_ADPCM_MS:
      return MEDIA_MIMETYPE_AUDIO_MS_ADPCM;
    default:
      return nullptr;
  }
}

// TODO(youfa) add more codec here
AVCodecID AvpCodecToCodecID(CodecType codecType) {
  switch (codecType) {
    // VIDEO
    case VIDEO_AVC:
      return AV_CODEC_ID_H264;
    case VIDEO_HEVC:
      return AV_CODEC_ID_HEVC;
    case VIDEO_MPEG4:
      return AV_CODEC_ID_MPEG4;
    case VIDEO_VP8:
      return AV_CODEC_ID_VP8;
    case VIDEO_VP9:
      return AV_CODEC_ID_VP9;

      // AUDIO
    case AUDIO_AAC:
      return AV_CODEC_ID_AAC;

    default:
      return AV_CODEC_ID_NONE;
  }
}

CodecType CodecIDToAvpCodec(AVCodecID codecId) {
  switch (codecId) {
    case AV_CODEC_ID_H264:
      return VIDEO_AVC;
    case AV_CODEC_ID_HEVC:
      return VIDEO_HEVC;
    case AV_CODEC_ID_MPEG4:
      return VIDEO_MPEG4;
    case AV_CODEC_ID_VP8:
      return VIDEO_VP8;
    case AV_CODEC_ID_VP9:
      return VIDEO_VP9;

    case AV_CODEC_ID_AAC:
      return AUDIO_AAC;

    default:
      return CODEC_UNKNOWN;
  }
}

int32_t pixelFormatToColorFormat(int32_t pixelFormat) {
  switch (pixelFormat) {
    case AV_PIX_FMT_YUV420P:
      return COLOR_FormatYUV420Planar;
    case AV_PIX_FMT_YUV422P:
      return COLOR_FormatYUV422Planar;
    default:
      LOG(LS_ERROR) << "Unsupported PixelFormat: " << pixelFormat;
  }
  return -1;
}

AVPixelFormat colorFormatToPixelFormat(int32_t colorFormat) {
  switch (colorFormat) {
    case COLOR_FormatYUV420Planar:
      return AV_PIX_FMT_YUV420P;
    case COLOR_FormatYUV422Planar:
      return AV_PIX_FMT_YUV422P;
    default:
      LOG(LS_ERROR) << "Unsupported PixelFormat: " << colorFormat;
  }
  return AV_PIX_FMT_NONE;
}

ChannelLayout ChannelLayoutToAvpChannelLayout(int64_t layout, int channels) {
  switch (layout) {
    case AV_CH_LAYOUT_MONO:
      return CHANNEL_LAYOUT_MONO;
    case AV_CH_LAYOUT_STEREO:
      return CHANNEL_LAYOUT_STEREO;
    case AV_CH_LAYOUT_2_1:
      return CHANNEL_LAYOUT_2_1;
    case AV_CH_LAYOUT_SURROUND:
      return CHANNEL_LAYOUT_SURROUND;
    case AV_CH_LAYOUT_4POINT0:
      return CHANNEL_LAYOUT_4_0;
    case AV_CH_LAYOUT_2_2:
      return CHANNEL_LAYOUT_2_2;
    case AV_CH_LAYOUT_QUAD:
      return CHANNEL_LAYOUT_QUAD;
    case AV_CH_LAYOUT_5POINT0:
      return CHANNEL_LAYOUT_5_0;
    case AV_CH_LAYOUT_5POINT1:
      return CHANNEL_LAYOUT_5_1;
    case AV_CH_LAYOUT_5POINT0_BACK:
      return CHANNEL_LAYOUT_5_0_BACK;
    case AV_CH_LAYOUT_5POINT1_BACK:
      return CHANNEL_LAYOUT_5_1_BACK;
    case AV_CH_LAYOUT_7POINT0:
      return CHANNEL_LAYOUT_7_0;
    case AV_CH_LAYOUT_7POINT1:
      return CHANNEL_LAYOUT_7_1;
    case AV_CH_LAYOUT_7POINT1_WIDE:
      return CHANNEL_LAYOUT_7_1_WIDE;
    case AV_CH_LAYOUT_STEREO_DOWNMIX:
      return CHANNEL_LAYOUT_STEREO_DOWNMIX;
    default:
      // FFmpeg channel_layout is 0 for .wav and .mp3.  We know mono and
      // stereo from the number of channels, otherwise report errors.
      if (channels == 1)
        return CHANNEL_LAYOUT_MONO;
      if (channels == 2)
        return CHANNEL_LAYOUT_STEREO;
      LOG(LS_DEBUG) << "Unsupported channel layout: " << layout;
  }
  return CHANNEL_LAYOUT_UNSUPPORTED;
}

void AVStreamToAudioMeta(const AVStream* audioStream,
                         std::shared_ptr<MetaData>& meta) {
  CHECK_NE(audioStream, nullptr);
  CHECK_EQ(audioStream->codecpar->codec_type, AVMEDIA_TYPE_AUDIO);

  meta->clear();

  LOG(LS_INFO) << "audio codec id:" << audioStream->codecpar->codec_id;
  const char* mime = AudioCodecId2Mime(audioStream->codecpar->codec_id);
  if (mime != nullptr) {
    meta->setCString(kKeyMIMEType, mime);
  }

  meta->setInt32(kKeyCodecType,
                 CodecIDToAvpCodec(audioStream->codecpar->codec_id));

  int32_t sampleRate = audioStream->codecpar->sample_rate;
  if (sampleRate > 0) {
    meta->setInt32(kKeySampleRate, sampleRate);
  }

  //  int32_t sampleFormat = audioStream->codecpar->format;

  meta->setInt32(kKeyChannelCount, audioStream->codecpar->channels);
  meta->setInt32(kKeyChannelMask, ChannelLayoutToAvpChannelLayout(
                                      audioStream->codecpar->channel_layout,
                                      audioStream->codecpar->channels));

  int32_t bitsPerSample = audioStream->codecpar->bits_per_coded_sample;
  LOG(LS_INFO) << "bitsPerSample:" << bitsPerSample
               << ", raw:" << audioStream->codecpar->bits_per_raw_sample;
  if (bitsPerSample > 0) {
    meta->setInt32(kKeyBitsPerSample, bitsPerSample);
  }
  if (audioStream->codecpar->extradata) {
    meta->setData(kKeyFFmpegExtraData, MetaData::TYPE_POINTER,
                  audioStream->codecpar->extradata,
                  audioStream->codecpar->extradata_size);
  }
}

void AVStreamToVideoMeta(const AVStream* videoStream,
                         std::shared_ptr<MetaData>& meta) {
  CHECK_NE(videoStream, nullptr);
  CHECK_EQ(videoStream->codecpar->codec_type, AVMEDIA_TYPE_VIDEO);
  meta->clear();

  const char* mime = VideoCodecId2Mime(videoStream->codecpar->codec_id);
  if (mime != nullptr) {
    meta->setCString(kKeyMIMEType, mime);
  }

  meta->setInt64(kKeyNumerator, videoStream->time_base.num);
  meta->setInt64(kKeyDenominator, videoStream->time_base.den);

  meta->setInt32(kKeyCodecType,
                 CodecIDToAvpCodec(videoStream->codecpar->codec_id));

  meta->setInt32(kKeyWidth, videoStream->codecpar->width);
  meta->setInt32(kKeyHeight, videoStream->codecpar->height);
  AVRational aspectRatio = {1, 1};
  if (videoStream->sample_aspect_ratio.num) {
    aspectRatio = videoStream->sample_aspect_ratio;
  } else if (videoStream->codecpar->sample_aspect_ratio.num) {
    aspectRatio = videoStream->codecpar->sample_aspect_ratio;
  }

  LOG(LS_INFO) << "Video Meta, extra:" << videoStream->codecpar->extradata
               << ", size:" << videoStream->codecpar->extradata_size;

  int32_t profile = videoStream->codecpar->profile;
  if (profile > 0) {
    meta->setInt32(kKeyVideoProfile, profile);
  }
  int32_t level = videoStream->codecpar->level;
  if (level > 0) {
    meta->setInt32(kKeyVideoLevel, level);
  }
  int32_t colorFormat = pixelFormatToColorFormat(videoStream->codecpar->format);
  if (colorFormat > 0) {
    meta->setInt32(kKeyColorFormat, colorFormat);
  }

  if (videoStream->codecpar->extradata) {
    LOG(LS_INFO) << "### extradata size:"
                 << videoStream->codecpar->extradata_size;
    hexdump(videoStream->codecpar->extradata,
            videoStream->codecpar->extradata_size);
    meta->setData(kKeyFFmpegExtraData, MetaData::TYPE_POINTER,
                  videoStream->codecpar->extradata,
                  videoStream->codecpar->extradata_size);
  }
}

void VideoFormatToAVCodecContext(const std::shared_ptr<Message>& format,
                                 AVCodecContext* codecContext) {
  codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
  codecContext->time_base = kMicrosBase;

  std::string mime;
  CHECK(format->findString("mime", mime));

  int32_t codecType;
  CHECK(format->findInt32("codec", &codecType));
  codecContext->codec_id = AvpCodecToCodecID(static_cast<CodecType>(codecType));
  int32_t profile;
  format->findInt32("profile", &profile);
  codecContext->profile = profile;
  int32_t width;
  CHECK(format->findInt32("width", &width));
  codecContext->coded_width = width;

  int32_t height;
  CHECK(format->findInt32("height", &height));
  codecContext->coded_height = height;

  int32_t colorFormat = -1;
  format->findInt32("color-format", &colorFormat);

  codecContext->pix_fmt = colorFormatToPixelFormat(colorFormat);

  std::shared_ptr<Buffer> exData;
  LOG(LS_INFO) << "VideoFormatToAVCodecContext";
  if (format->findBuffer("ffmpeg-exdata", exData)) {
    LOG(LS_INFO) << "has ffmpeg-exdata";
    codecContext->extradata_size = exData->size();
    codecContext->extradata = reinterpret_cast<uint8_t*>(
        av_malloc(exData->size() + AV_INPUT_BUFFER_PADDING_SIZE));
    memcpy(codecContext->extradata, exData->data(), exData->size());
    memset(codecContext->extradata + exData->size(), '\0',
           AV_INPUT_BUFFER_PADDING_SIZE);
  } else {
    codecContext->extradata = nullptr;
    codecContext->extradata_size = 0;
  }
}

void AudioFormatToAVCodecContext(const std::shared_ptr<Message>& format,
                                 AVCodecContext* codecContext) {
  codecContext->codec_type = AVMEDIA_TYPE_AUDIO;
  int32_t codecType;
  CHECK(format->findInt32("codec", &codecType));
  LOG(LS_INFO) << "------------------ codecType:" << codecType << ", id:"
               << AvpCodecToCodecID(static_cast<CodecType>(codecType));
  codecContext->codec_id = AvpCodecToCodecID(static_cast<CodecType>(codecType));

  int32_t bitsPerSample;
  CHECK(format->findInt32("bits-per-sample", &bitsPerSample));
  LOG(LS_INFO) << "bitsPerSample:" << bitsPerSample;

  switch (bitsPerSample) {
    case 8:
      codecContext->sample_fmt = AV_SAMPLE_FMT_U8;
      break;
    case 16:
      codecContext->sample_fmt = AV_SAMPLE_FMT_S16;
      break;
    case 32:
      codecContext->sample_fmt = AV_SAMPLE_FMT_S32;
      break;
    default:
      LOG(LS_ERROR) << "Unsupported bits per channel: " << bitsPerSample;
      codecContext->sample_fmt = AV_SAMPLE_FMT_NONE;
  }

  int32_t channels;
  CHECK(format->findInt32("channel-count", &channels));
  codecContext->channels = channels;

  int32_t channelLayout;
  CHECK(format->findInt32("channel-mask", &channelLayout));
  codecContext->channel_layout = channelLayout;

  int32_t sampleRate;
  CHECK(format->findInt32("sample-rate", &sampleRate));
  codecContext->sample_rate = sampleRate;

  std::shared_ptr<Buffer> exData;
  if (format->findBuffer("ffmpeg-exdata", exData)) {
    codecContext->extradata_size = exData->size();
    codecContext->extradata = reinterpret_cast<uint8_t*>(
        av_malloc(exData->size() + AV_INPUT_BUFFER_PADDING_SIZE));
    memcpy(codecContext->extradata, exData->data(), exData->size());
    memset(codecContext->extradata + exData->size(), '\0',
           AV_INPUT_BUFFER_PADDING_SIZE);
  }
}

std::shared_ptr<Buffer> createBufferFromAvPacket(const AVPacket* pkt,
                                                 const AVRational& time_base) {
  std::shared_ptr<Buffer> buffer = Buffer::CreateAsCopy(pkt->data, pkt->size);
  buffer->meta()->setInt64("timeUs", ConvertFromTimeBase(time_base, pkt->pts));

  return buffer;
}

std::shared_ptr<Buffer> createAudioBufferFromAvFrame(
    const AVFrame* frame,
    const AVRational& time_base) {
  int sampleSize =
      av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
  if (sampleSize < 0) {
    return nullptr;
  }
  // LOG(LS_INFO) << "create audio buffer, sampleSize:" << sampleSize
  //             << ", nb_samples:" << frame->nb_samples
  //             << ", channels:" << frame->channels;
  int nbSamples = frame->nb_samples;
  int32_t size = sampleSize * nbSamples * frame->channels;
  auto buffer = std::make_shared<Buffer>(size);

  for (int i = 0; i < nbSamples; i++) {
    for (int ch = 0; ch < frame->channels; ch++) {
      int offset = i * sampleSize * frame->channels + sampleSize * ch;
      memcpy(buffer->data() + offset, frame->data[ch] + sampleSize * i,
             sampleSize);
    }
  }
  // hexdump(buffer->data(), 100);
  buffer->meta()->setInt64("timeUs",
                           ConvertFromTimeBase(time_base, frame->pts));

  return buffer;
}

std::shared_ptr<Buffer> createVideoBufferFromAvFrame(
    const AVFrame* frame,
    const AVRational& time_base) {
  // only support 420p and 422p now
  CHECK((frame->format == AV_PIX_FMT_YUV420P) ||
        (frame->format == AV_PIX_FMT_YUV422P));

  int32_t size = frame->linesize[0] * frame->height * 3 / 2;
  auto buffer = std::make_shared<Buffer>(size);

  // LOG(LS_INFO) << "######## create bufer, stride:" << frame->linesize[0];
  int32_t uOffset = frame->linesize[0] * frame->height;
  int32_t vOffset = frame->linesize[0] * frame->height * 5 / 4;
  memcpy(buffer->data(), frame->data[0], frame->linesize[0] * frame->height);
  memcpy(buffer->data() + uOffset, frame->data[1],
         frame->linesize[0] * frame->height / 4);
  memcpy(buffer->data() + vOffset, frame->data[2],
         frame->linesize[0] * frame->height / 4);

  buffer->meta()->setInt64("timeUs",
                           ConvertFromTimeBase(time_base, frame->pts));
  buffer->meta()->setInt32("width", frame->width);
  buffer->meta()->setInt32("height", frame->height);
  buffer->meta()->setInt32("stride", frame->linesize[0]);

  return buffer;
}

} /* namespace avp */
