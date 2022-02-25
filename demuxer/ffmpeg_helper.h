/*
 * ffmpeg_helper.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_HELPER_H
#define FFMPEG_HELPER_H

#include <memory>

#include "common/buffer.h"
#include "player/media_defs.h"
#include "player/meta_data.h"

extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
#include "third_party/ffmpeg/libavformat/avio.h"
#include "third_party/ffmpeg/libavutil/avutil.h"
}

namespace avp {

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
      //    case AV_CODEC_ID_AMR_NB:
      //      return MEDIA_MIMETYPE_AUDIO_EAC3_JOC;
      //    case AV_CODEC_ID_AC4:
      //      return MEDIA_MIMETYPE_AUDIO_AC4;
      //    case AV_CODEC_ID_MPEGH_3D_AUDIO:
      //      return MEDIA_MIMETYPE_AUDIO_MPEGH_MHA1;
      //    case AV_CODEC_ID_AMR_NB:
      //      return MEDIA_MIMETYPE_AUDIO_MPEGH_MHM1;
      //    case AV_CODEC_ID_SCR:
      //      return MEDIA_MIMETYPE_AUDIO_SCRAMBLED;
    case AV_CODEC_ID_ALAC:
      return MEDIA_MIMETYPE_AUDIO_ALAC;
    case AV_CODEC_ID_WMAV1:
    case AV_CODEC_ID_WMAV2:
      return MEDIA_MIMETYPE_AUDIO_WMA;
    case AV_CODEC_ID_ADPCM_MS:
      return MEDIA_MIMETYPE_AUDIO_MS_ADPCM;
      //    case AV_CODEC_ID_ADPCM_IMAD:
      //      return MEDIA_MIMETYPE_AUDIO_DVI_IMA_ADPCM;
    default:
      return "not support";
  }
}

std::shared_ptr<Buffer> createBufferFromAvPacket(AVPacket* pkt) {
  std::shared_ptr<Buffer> buffer = Buffer::CreateAsCopy(pkt->data, pkt->size);
  buffer->meta()->setInt64("timeUs", pkt->pts);

  return buffer;
}

} /* namespace avp */

#endif /* !FFMPEG_HELPER_H */
