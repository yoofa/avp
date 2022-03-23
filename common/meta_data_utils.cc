/*
 * meta_data_utils.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "meta_data_utils.h"

#include <cstring>

#include "base/logging.h"
#include "common/avc_utils.h"
#include "common/bit_reader.h"
#include "common/buffer.h"
#include "common/media_defs.h"
#include "common/utils.h"

namespace avp {

bool MakeAVCCodecSpecificData(MetaData& meta,
                              const uint8_t* data,
                              size_t size) {
  if (data == nullptr || size == 0) {
    return false;
  }

  int32_t width;
  int32_t height;
  int32_t sarWidth;
  int32_t sarHeight;
  std::shared_ptr<Buffer> accessUnit =
      std::make_shared<Buffer>((void*)(data), size);
  std::shared_ptr<Buffer> csd = MakeAVCCodecSpecificData(
      accessUnit, &width, &height, &sarWidth, &sarHeight);
  if (csd == nullptr) {
    return false;
  }
  meta.setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);

  meta.setData(kKeyAVCC, kTypeAVCC, csd->data(), csd->size());
  meta.setInt32(kKeyWidth, width);
  meta.setInt32(kKeyHeight, height);
  if (sarWidth > 0 && sarHeight > 0) {
    meta.setInt32(kKeySARWidth, sarWidth);
    meta.setInt32(kKeySARHeight, sarHeight);
  }
  return true;
}

bool MakeAVCCodecSpecificData(std::shared_ptr<Message>& meta,
                              const uint8_t* data,
                              size_t size) {
  if (meta == nullptr || data == nullptr || size == 0) {
    return false;
  }

  int32_t width;
  int32_t height;
  int32_t sarWidth;
  int32_t sarHeight;
  std::shared_ptr<Buffer> accessUnit =
      std::make_shared<Buffer>((void*)data, size);
  std::shared_ptr<Buffer> csd = MakeAVCCodecSpecificData(
      accessUnit, &width, &height, &sarWidth, &sarHeight);
  if (csd == nullptr) {
    return false;
  }
  meta->setString("mime", MEDIA_MIMETYPE_VIDEO_AVC);

  meta->setBuffer("csd-avc", csd);
  meta->setInt32("width", width);
  meta->setInt32("height", height);
  if (sarWidth > 0 && sarHeight > 0) {
    meta->setInt32("sar-width", sarWidth);
    meta->setInt32("sar-height", sarHeight);
  }
  return true;
}

bool MakeAACCodecSpecificData(MetaData& meta,
                              const uint8_t* data,
                              size_t size) {
  if (data == nullptr || size < 7) {
    return false;
  }

  BitReader bits(data, size);

  // adts_fixed_header

  if (bits.getBits(12) != 0xfffu) {
    LOG(LS_INFO) << "Wrong atds_fixed_header";
    return false;
  }

  bits.skipBits(4);  // ID, layer, protection_absent

  unsigned profile = bits.getBits(2);
  if (profile == 3u) {
    LOG(LS_INFO) << "profile should not be 3";
    return false;
  }
  unsigned sampling_freq_index = bits.getBits(4);
  bits.getBits(1);  // private_bit
  unsigned channel_configuration = bits.getBits(3);
  if (channel_configuration == 0u) {
    LOG(LS_INFO) << "channel_config should not be 0";
    return false;
  }

  if (!MakeAACCodecSpecificData(meta, profile, sampling_freq_index,
                                channel_configuration)) {
    return false;
  }

  meta.setInt32(kKeyIsADTS, true);
  return true;
}

bool MakeAACCodecSpecificData(uint8_t* csd,                   /* out */
                              size_t* esds_size,              /* in/out */
                              unsigned profile,               /* in */
                              unsigned sampling_freq_index,   /* in */
                              unsigned channel_configuration, /* in */
                              int32_t* sampling_rate          /* out */
) {
  if (sampling_freq_index > 11u) {
    return false;
  }
  static const int32_t kSamplingFreq[] = {96000, 88200, 64000, 48000,
                                          44100, 32000, 24000, 22050,
                                          16000, 12000, 11025, 8000};
  *sampling_rate = kSamplingFreq[sampling_freq_index];

  static const uint8_t kStaticESDS[] = {
      0x03, 22,   0x00, 0x00,  // ES_ID
      0x00,                    // streamDependenceFlag, URL_Flag, OCRstreamFlag

      0x04, 17,
      0x40,  // Audio ISO/IEC 14496-3
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

      0x05, 2,
      // AudioSpecificInfo follows

      // oooo offf fccc c000
      // o - audioObjectType
      // f - samplingFreqIndex
      // c - channelConfig
  };

  size_t csdSize = sizeof(kStaticESDS) + 2;
  if (csdSize > *esds_size) {
    return false;
  }
  memcpy(csd, kStaticESDS, sizeof(kStaticESDS));

  csd[sizeof(kStaticESDS)] = ((profile + 1) << 3) | (sampling_freq_index >> 1);

  csd[sizeof(kStaticESDS) + 1] =
      ((sampling_freq_index << 7) & 0x80) | (channel_configuration << 3);

  *esds_size = csdSize;
  return true;
}

bool MakeAACCodecSpecificData(std::shared_ptr<Message>& meta,
                              unsigned profile,
                              unsigned sampling_freq_index,
                              unsigned channel_configuration) {
  if (sampling_freq_index > 11u) {
    return false;
  }

  uint8_t csd[2];
  csd[0] = ((profile + 1) << 3) | (sampling_freq_index >> 1);
  csd[1] = ((sampling_freq_index << 7) & 0x80) | (channel_configuration << 3);

  static const int32_t kSamplingFreq[] = {96000, 88200, 64000, 48000,
                                          44100, 32000, 24000, 22050,
                                          16000, 12000, 11025, 8000};
  int32_t sampleRate = kSamplingFreq[sampling_freq_index];

  auto csdBuffer = std::make_shared<Buffer>(csd, sizeof(csd));

  meta->setBuffer("csd-0", csdBuffer);
  meta->setString("mime", MEDIA_MIMETYPE_AUDIO_AAC);
  meta->setInt32("sample-rate", sampleRate);
  meta->setInt32("channel-count", channel_configuration);

  return true;
}

bool MakeAACCodecSpecificData(MetaData& meta,
                              unsigned profile,
                              unsigned sampling_freq_index,
                              unsigned channel_configuration) {
  uint8_t csd[24];
  size_t csdSize = sizeof(csd);
  int32_t sampleRate;

  if (!MakeAACCodecSpecificData(csd, &csdSize, profile, sampling_freq_index,
                                channel_configuration, &sampleRate)) {
    return false;
  }

  meta.setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AAC);

  meta.setInt32(kKeySampleRate, sampleRate);
  meta.setInt32(kKeyChannelCount, channel_configuration);
  meta.setData(kKeyESDS, 0, csd, csdSize);
  return true;
}
/*

static void extractAlbumArt(AMediaFormat* fileMeta,
                            const void* data,
                            size_t size) {
  ALOGV("extractAlbumArt from '%s'", (const char*)data);

  size_t inLen = strnlen((const char*)data, size);
  size_t flacSize = inLen / 4 * 3;
  uint8_t* flac = new uint8_t[flacSize];
  if (!decodeBase64(flac, &flacSize, (const char*)data)) {
    LOG(LS_INFO) << "malformed base64 encoded data.");
    delete[] flac;
    return;
  }

  ALOGV("got flac of size %zu", flacSize);

  uint32_t picType;
  uint32_t typeLen;
  uint32_t descLen;
  uint32_t dataLen;
  char type[128];

  if (flacSize < 8) {
    delete[] flac;
    return;
  }

  picType = U32_AT(flac);

  if (picType != 3) {
    // This is not a front cover.
    delete[] flac;
    return;
  }

  typeLen = U32_AT(&flac[4]);
  if (typeLen > sizeof(type) - 1) {
    delete[] flac;
    return;
  }

  // we've already checked above that flacSize >= 8
  if (flacSize - 8 < typeLen) {
    delete[] flac;
    return;
  }

  memcpy(type, &flac[8], typeLen);
  type[typeLen] = '\0';

  ALOGV("picType = %d, type = '%s'", picType, type);

  if (!strcmp(type, "-->")) {
    // This is not inline cover art, but an external url instead.
    delete[] flac;
    return;
  }

  if (flacSize < 32 || flacSize - 32 < typeLen) {
    delete[] flac;
    return;
  }

  descLen = U32_AT(&flac[8 + typeLen]);
  if (flacSize - 32 - typeLen < descLen) {
    delete[] flac;
    return;
  }

  dataLen = U32_AT(&flac[8 + typeLen + 4 + descLen + 16]);

  // we've already checked above that (flacSize - 32 - typeLen - descLen) >= 0
  if (flacSize - 32 - typeLen - descLen < dataLen) {
    delete[] flac;
    return;
  }

  ALOGV("got image data, %zu trailing bytes",
        flacSize - 32 - typeLen - descLen - dataLen);

  meta->setBuffer(fileMeta, AMEDIAFORMAT_KEY_ALBUMART,
                  &flac[8 + typeLen + 4 + descLen + 20], dataLen);

  delete[] flac;
}

void parseVorbisComment(AMediaFormat* fileMeta,
                        const char* comment,
                        size_t commentLength) {
  // Haptic tag is only kept here as it will only be used in extractor to
  // generate channel mask.
  struct {
    const char* const mTag;
    const char* mKey;
  } kMap[] = {
      {"TITLE", AMEDIAFORMAT_KEY_TITLE},
      {"ARTIST", AMEDIAFORMAT_KEY_ARTIST},
      {"ALBUMARTIST", AMEDIAFORMAT_KEY_ALBUMARTIST},
      {"ALBUM ARTIST", AMEDIAFORMAT_KEY_ALBUMARTIST},
      {"COMPILATION", AMEDIAFORMAT_KEY_COMPILATION},
      {"ALBUM", AMEDIAFORMAT_KEY_ALBUM},
      {"COMPOSER", AMEDIAFORMAT_KEY_COMPOSER},
      {"GENRE", AMEDIAFORMAT_KEY_GENRE},
      {"AUTHOR", AMEDIAFORMAT_KEY_AUTHOR},
      {"TRACKNUMBER", AMEDIAFORMAT_KEY_CDTRACKNUMBER},
      {"DISCNUMBER", AMEDIAFORMAT_KEY_DISCNUMBER},
      {"DATE", AMEDIAFORMAT_KEY_DATE},
      {"YEAR", AMEDIAFORMAT_KEY_YEAR},
      {"LYRICIST", AMEDIAFORMAT_KEY_LYRICIST},
      {"METADATA_BLOCK_PICTURE", AMEDIAFORMAT_KEY_ALBUMART},
      {"ANDROID_LOOP", AMEDIAFORMAT_KEY_LOOP},
      {"ANDROID_HAPTIC", AMEDIAFORMAT_KEY_HAPTIC_CHANNEL_COUNT},
  };

  for (size_t j = 0; j < sizeof(kMap) / sizeof(kMap[0]); ++j) {
    size_t tagLen = strlen(kMap[j].mTag);
    if (!strncasecmp(kMap[j].mTag, comment, tagLen) && comment[tagLen] == '=') {
      if (kMap[j].mKey == AMEDIAFORMAT_KEY_ALBUMART) {
        extractAlbumArt(fileMeta, &comment[tagLen + 1],
                        commentLength - tagLen - 1);
      } else if (kMap[j].mKey == AMEDIAFORMAT_KEY_LOOP) {
        if (!strcasecmp(&comment[tagLen + 1], "true")) {
          meta->setInt32(fileMeta, AMEDIAFORMAT_KEY_LOOP, 1);
        }
      } else if (kMap[j].mKey == AMEDIAFORMAT_KEY_HAPTIC_CHANNEL_COUNT) {
        char* end;
        errno = 0;
        const int hapticChannelCount = strtol(&comment[tagLen + 1], &end, 10);
        if (errno == 0) {
          meta->setInt32(fileMeta, kMap[j].mKey, hapticChannelCount);
        } else {
          LOG(LS_INFO) << "Error(%d) when parsing haptic channel count", errno);
        }
      } else {
        meta->setString(fileMeta, kMap[j].mKey, &comment[tagLen + 1]);
      }
    }
  }
}
*/
} /* namespace avp */
