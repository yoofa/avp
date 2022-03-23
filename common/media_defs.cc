/*
 * media_defs.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "media_defs.h"

#include <string.h>
#include <strings.h>

namespace avp {

const char* MEDIA_MIMETYPE_IMAGE_JPEG = "image/jpeg";
const char* MEDIA_MIMETYPE_IMAGE_ANDROID_HEIC = "image/vnd.android.heic";
const char* MEDIA_MIMETYPE_IMAGE_AVIF = "image/avif";

const char* MEDIA_MIMETYPE_VIDEO_VP8 = "video/x-vnd.on2.vp8";
const char* MEDIA_MIMETYPE_VIDEO_VP9 = "video/x-vnd.on2.vp9";
const char* MEDIA_MIMETYPE_VIDEO_AV1 = "video/av01";
const char* MEDIA_MIMETYPE_VIDEO_AVC = "video/avc";
const char* MEDIA_MIMETYPE_VIDEO_HEVC = "video/hevc";
const char* MEDIA_MIMETYPE_VIDEO_MPEG4 = "video/mp4v-es";
const char* MEDIA_MIMETYPE_VIDEO_H263 = "video/3gpp";
const char* MEDIA_MIMETYPE_VIDEO_MPEG2 = "video/mpeg2";
const char* MEDIA_MIMETYPE_VIDEO_RAW = "video/raw";
const char* MEDIA_MIMETYPE_VIDEO_DOLBY_VISION = "video/dolby-vision";
const char* MEDIA_MIMETYPE_VIDEO_DIVX = "video/divx";
const char* MEDIA_MIMETYPE_VIDEO_DIVX3 = "video/divx3";
const char* MEDIA_MIMETYPE_VIDEO_XVID = "video/xvid";
const char* MEDIA_MIMETYPE_VIDEO_MJPEG = "video/x-motion-jpeg";

const char* MEDIA_MIMETYPE_AUDIO_AMR_NB = "audio/3gpp";
const char* MEDIA_MIMETYPE_AUDIO_AMR_WB = "audio/amr-wb";
const char* MEDIA_MIMETYPE_AUDIO_MPEG = "audio/mpeg";
const char* MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_I = "audio/mpeg-L1";
const char* MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II = "audio/mpeg-L2";
const char* MEDIA_MIMETYPE_AUDIO_MIDI = "audio/midi";
const char* MEDIA_MIMETYPE_AUDIO_AAC = "audio/mp4a-latm";
const char* MEDIA_MIMETYPE_AUDIO_QCELP = "audio/qcelp";
const char* MEDIA_MIMETYPE_AUDIO_VORBIS = "audio/vorbis";
const char* MEDIA_MIMETYPE_AUDIO_OPUS = "audio/opus";
const char* MEDIA_MIMETYPE_AUDIO_G711_ALAW = "audio/g711-alaw";
const char* MEDIA_MIMETYPE_AUDIO_G711_MLAW = "audio/g711-mlaw";
const char* MEDIA_MIMETYPE_AUDIO_RAW = "audio/raw";
const char* MEDIA_MIMETYPE_AUDIO_FLAC = "audio/flac";
const char* MEDIA_MIMETYPE_AUDIO_AAC_ADTS = "audio/aac-adts";
const char* MEDIA_MIMETYPE_AUDIO_MSGSM = "audio/gsm";
const char* MEDIA_MIMETYPE_AUDIO_AC3 = "audio/ac3";
const char* MEDIA_MIMETYPE_AUDIO_EAC3 = "audio/eac3";
const char* MEDIA_MIMETYPE_AUDIO_EAC3_JOC = "audio/eac3-joc";
const char* MEDIA_MIMETYPE_AUDIO_AC4 = "audio/ac4";
const char* MEDIA_MIMETYPE_AUDIO_MPEGH_MHA1 = "audio/mha1";
const char* MEDIA_MIMETYPE_AUDIO_MPEGH_MHM1 = "audio/mhm1";
const char* MEDIA_MIMETYPE_AUDIO_ALAC = "audio/alac";
const char* MEDIA_MIMETYPE_AUDIO_WMA = "audio/x-ms-wma";
const char* MEDIA_MIMETYPE_AUDIO_MS_ADPCM = "audio/x-adpcm-ms";
const char* MEDIA_MIMETYPE_AUDIO_DVI_IMA_ADPCM = "audio/x-adpcm-dvi-ima";

const char* MEDIA_MIMETYPE_CONTAINER_MPEG4 = "video/mp4";
const char* MEDIA_MIMETYPE_CONTAINER_WAV = "audio/x-wav";
const char* MEDIA_MIMETYPE_CONTAINER_OGG = "audio/ogg";
const char* MEDIA_MIMETYPE_CONTAINER_MATROSKA = "video/x-matroska";
const char* MEDIA_MIMETYPE_CONTAINER_MPEG2TS = "video/mp2ts";
const char* MEDIA_MIMETYPE_CONTAINER_AVI = "video/avi";
const char* MEDIA_MIMETYPE_CONTAINER_MPEG2PS = "video/mp2p";
const char* MEDIA_MIMETYPE_CONTAINER_HEIF = "image/heif";

const char* MEDIA_MIMETYPE_TEXT_3GPP = "text/3gpp-tt";
const char* MEDIA_MIMETYPE_TEXT_SUBRIP = "application/x-subrip";
const char* MEDIA_MIMETYPE_TEXT_VTT = "text/vtt";
const char* MEDIA_MIMETYPE_TEXT_CEA_608 = "text/cea-608";
const char* MEDIA_MIMETYPE_TEXT_CEA_708 = "text/cea-708";
const char* MEDIA_MIMETYPE_DATA_TIMED_ID3 = "application/x-id3v4";

CodecType mimeToCodec(const char* mime) {
  // VIDEO
  if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_VP8, mime, strlen(mime))) {
    return VIDEO_VP8;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_VP9, mime, strlen(mime))) {
    return VIDEO_VP9;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_AV1, mime, strlen(mime))) {
    return VIDEO_AV1;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_AVC, mime, strlen(mime))) {
    return VIDEO_AVC;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_HEVC, mime, strlen(mime))) {
    return VIDEO_HEVC;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_MPEG4, mime, strlen(mime))) {
    return VIDEO_MPEG4;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_H263, mime, strlen(mime))) {
    return VIDEO_H263;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_MPEG2, mime, strlen(mime))) {
    return VIDEO_MPEG2;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_RAW, mime, strlen(mime))) {
    return VIDEO_RAW;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_DOLBY_VISION, mime,
                          strlen(mime))) {
    return VIDEO_DOLBY_VISION;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_DIVX, mime, strlen(mime))) {
    return VIDEO_DIVX;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_DIVX3, mime, strlen(mime))) {
    return VIDEO_DIVX3;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_XVID, mime, strlen(mime))) {
    return VIDEO_XVID;
  } else if (!strncasecmp(MEDIA_MIMETYPE_VIDEO_MJPEG, mime, strlen(mime))) {
    return VIDEO_MJPEG;

    // AUDIO
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_AMR_NB, mime, strlen(mime))) {
    return AUDIO_AMR_NB;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_AMR_WB, mime, strlen(mime))) {
    return AUDIO_AMR_WB;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MPEG, mime, strlen(mime))) {
    return AUDIO_MPEG;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_I, mime,
                          strlen(mime))) {
    return AUDIO_MPEG_LAYER_I;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II, mime,
                          strlen(mime))) {
    return AUDIO_MPEG_LAYER_II;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MIDI, mime, strlen(mime))) {
    return AUDIO_MIDI;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_AAC, mime, strlen(mime))) {
    return AUDIO_AAC;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_QCELP, mime, strlen(mime))) {
    return AUDIO_QCELP;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_VORBIS, mime, strlen(mime))) {
    return AUDIO_VORBIS;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_OPUS, mime, strlen(mime))) {
    return AUDIO_OPUS;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_G711_ALAW, mime, strlen(mime))) {
    return AUDIO_G711_ALAW;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_G711_MLAW, mime, strlen(mime))) {
    return AUDIO_G711_MLAW;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_RAW, mime, strlen(mime))) {
    return AUDIO_RAW;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_FLAC, mime, strlen(mime))) {
    return AUDIO_FLAC;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_AAC_ADTS, mime, strlen(mime))) {
    return AUDIO_AAC_ADTS;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MSGSM, mime, strlen(mime))) {
    return AUDIO_MSGSM;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_AC3, mime, strlen(mime))) {
    return AUDIO_AC3;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_EAC3, mime, strlen(mime))) {
    return AUDIO_EAC3;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_EAC3_JOC, mime, strlen(mime))) {
    return AUDIO_EAC3_JOC;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_AC4, mime, strlen(mime))) {
    return AUDIO_AC4;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MPEGH_MHA1, mime,
                          strlen(mime))) {
    return AUDIO_MPEGH_MHA1;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MPEGH_MHM1, mime,
                          strlen(mime))) {
    return AUDIO_MPEGH_MHM1;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_ALAC, mime, strlen(mime))) {
    return AUDIO_ALAC;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_WMA, mime, strlen(mime))) {
    return AUDIO_WMA;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_MS_ADPCM, mime, strlen(mime))) {
    return AUDIO_MS_ADPCM;
  } else if (!strncasecmp(MEDIA_MIMETYPE_AUDIO_DVI_IMA_ADPCM, mime,
                          strlen(mime))) {
    return AUDIO_DVI_IMA_ADPCM;

    // UNKNOWN
  } else {
    return CODEC_UNKNOWN;
  }
}

} /* namespace avp */
