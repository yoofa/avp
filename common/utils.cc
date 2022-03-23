/*
 * utils.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "utils.h"

#include <memory>

#include "base/byte_utils.h"
#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "common/Lookup.h"
#include "common/buffer.h"
#include "common/codec_constants.h"
#include "common/color_utils.h"
#include "common/esds.h"
#include "common/hevc_utils.h"
#include "common/media_defs.h"
#include "common/message.h"
#include "common/meta_data.h"

#define AMEDIAFORMAT_KEY_MPEGH_PROFILE_LEVEL_INDICATION \
  "mpegh-profile-level-indication"
#define AMEDIAFORMAT_KEY_MPEGH_REFERENCE_CHANNEL_LAYOUT \
  "mpegh-reference-channel-layout"
#define AMEDIAFORMAT_KEY_MPEGH_COMPATIBLE_SETS "mpegh-compatible-sets"

namespace {
// TODO: this should possibly be handled in an else
// constexpr static int32_t AACObjectNull = 0;

// TODO: decide if we should just not transmit the level in this case
constexpr static int32_t DolbyVisionLevelUnknown = 0;
}  // namespace

namespace avp {
static status_t copyNALUToBuffer(std::shared_ptr<Buffer>& buffer,
                                 const uint8_t* ptr,
                                 size_t length) {
  if ((buffer->size() + 4 + length) > (buffer->capacity() - buffer->offset())) {
    std::shared_ptr<Buffer> tmpBuffer =
        std::make_shared<Buffer>(buffer->size() + 4 + length + 1024);
    if (tmpBuffer.get() == nullptr || tmpBuffer->base() == nullptr) {
      return NO_MEMORY;
    }
    memcpy(tmpBuffer->data(), buffer->data(), buffer->size());
    tmpBuffer->setRange(0, buffer->size());
    buffer = tmpBuffer;
  }

  memcpy(buffer->data() + buffer->size(), "\x00\x00\x00\x01", 4);

  memcpy(buffer->data() + buffer->size() + 4, ptr, length);
  buffer->setRange(buffer->offset(), buffer->size() + 4 + length);
  return OK;
}

static void convertMetaDataToMessageColorAspects(
    const MetaData* meta,
    std::shared_ptr<Message>& msg) {
  // 0 values are unspecified
  int32_t range = 0;
  int32_t primaries = 0;
  int32_t transferFunction = 0;
  int32_t colorMatrix = 0;
  meta->findInt32(kKeyColorRange, &range);
  meta->findInt32(kKeyColorPrimaries, &primaries);
  meta->findInt32(kKeyTransferFunction, &transferFunction);
  meta->findInt32(kKeyColorMatrix, &colorMatrix);
  ColorAspects colorAspects;
  memset(&colorAspects, 0, sizeof(colorAspects));
  colorAspects.mRange = (ColorAspects::Range)range;
  colorAspects.mPrimaries = (ColorAspects::Primaries)primaries;
  colorAspects.mTransfer = (ColorAspects::Transfer)transferFunction;
  colorAspects.mMatrixCoeffs = (ColorAspects::MatrixCoeffs)colorMatrix;

  int32_t rangeMsg, standardMsg, transferMsg;
  if (ColorUtils::convertCodecColorAspectsToPlatformAspects(
          colorAspects, &rangeMsg, &standardMsg, &transferMsg) != OK) {
    return;
  }

  // save specified values to msg
  if (rangeMsg != 0) {
    msg->setInt32("color-range", rangeMsg);
  }
  if (standardMsg != 0) {
    msg->setInt32("color-standard", standardMsg);
  }
  if (transferMsg != 0) {
    msg->setInt32("color-transfer", transferMsg);
  }
}
static bool isHdr(const std::shared_ptr<Message>& format) {
  // if CSD specifies HDR transfer(s), we assume HDR. Otherwise, if it specifies
  // non-HDR transfers, we must assume non-HDR. This is because CSD trumps any
  // color-transfer key in the format.
  int32_t isHdr;
  if (format->findInt32("android._is-hdr", &isHdr)) {
    return isHdr;
  }

  // if user/container supplied HDR static info without transfer set, assume
  // true
  if ((format->contains("hdr-static-info") ||
       format->contains("hdr10-plus-info")) &&
      !format->contains("color-transfer")) {
    return true;
  }
  // otherwise, verify that an HDR transfer function is set
  int32_t transfer;
  if (format->findInt32("color-transfer", &transfer)) {
    return transfer == ColorUtils::kColorTransferST2084 ||
           transfer == ColorUtils::kColorTransferHLG;
  }
  return false;
}

static void parseAacProfileFromCsd(const std::shared_ptr<Buffer>& csd,
                                   std::shared_ptr<Message>& format) {
  if (csd->size() < 2) {
    return;
  }

  uint16_t audioObjectType = U16_AT((uint8_t*)csd->data());
  if ((audioObjectType & 0xF800) == 0xF800) {
    audioObjectType = 32 + ((audioObjectType >> 5) & 0x3F);
  } else {
    audioObjectType >>= 11;
  }

  const static Lookup<uint16_t, int32_t> profiles{
      {1, AACObjectMain},  {2, AACObjectLC},   {3, AACObjectSSR},
      {4, AACObjectLTP},   {5, AACObjectHE},   {6, AACObjectScalable},
      {17, AACObjectERLC}, {23, AACObjectLD},  {29, AACObjectHE_PS},
      {39, AACObjectELD},  {42, AACObjectXHE},
  };

  int32_t profile;
  if (profiles.map(audioObjectType, &profile)) {
    format->setInt32("profile", profile);
  }
}

static void parseAvcProfileLevelFromAvcc(const uint8_t* ptr,
                                         size_t size,
                                         std::shared_ptr<Message>& format) {
  if (size < 4 || ptr[0] != 1) {  // configurationVersion == 1
    return;
  }
  const uint8_t profile = ptr[1];
  const uint8_t constraints = ptr[2];
  const uint8_t level = ptr[3];

  const static Lookup<uint8_t, int32_t> levels{
      {9, AVCLevel1b},  // technically, 9 is only used for High+ profiles
      {10, AVCLevel1},  {11, AVCLevel11},  // prefer level 1.1 for the value 11
      {11, AVCLevel1b}, {12, AVCLevel12}, {13, AVCLevel13}, {20, AVCLevel2},
      {21, AVCLevel21}, {22, AVCLevel22}, {30, AVCLevel3},  {31, AVCLevel31},
      {32, AVCLevel32}, {40, AVCLevel4},  {41, AVCLevel41}, {42, AVCLevel42},
      {50, AVCLevel5},  {51, AVCLevel51}, {52, AVCLevel52}, {60, AVCLevel6},
      {61, AVCLevel61}, {62, AVCLevel62},
  };
  const static Lookup<uint8_t, int32_t> profiles{
      {66, AVCProfileBaseline}, {77, AVCProfileMain},
      {88, AVCProfileExtended}, {100, AVCProfileHigh},
      {110, AVCProfileHigh10},  {122, AVCProfileHigh422},
      {244, AVCProfileHigh444},
  };

  // set profile & level if they are recognized
  int32_t codecProfile;
  int32_t codecLevel;
  if (profiles.map(profile, &codecProfile)) {
    if (profile == 66 && (constraints & 0x40)) {
      codecProfile = AVCProfileConstrainedBaseline;
    } else if (profile == 100 && (constraints & 0x0C) == 0x0C) {
      codecProfile = AVCProfileConstrainedHigh;
    }
    format->setInt32("profile", codecProfile);
    if (levels.map(level, &codecLevel)) {
      // for 9 && 11 decide level based on profile and constraint_set3 flag
      if (level == 11 && (profile == 66 || profile == 77 || profile == 88)) {
        codecLevel = (constraints & 0x10) ? AVCLevel1b : AVCLevel11;
      }
      format->setInt32("level", codecLevel);
    }
  }
}

static void parseDolbyVisionProfileLevelFromDvcc(
    const uint8_t* ptr,
    size_t size,
    std::shared_ptr<Message>& format) {
  // dv_major.dv_minor Should be 1.0 or 2.1
  if (size != 24 ||
      ((ptr[0] != 1 || ptr[1] != 0) && (ptr[0] != 2 || ptr[1] != 1))) {
    LOG(LS_VERBOSE) << "Size " << size << ", dv_major " << ptr[0]
                    << ", dv_minor " << ptr[1];
    return;
  }

  const uint8_t profile = ptr[2] >> 1;
  const uint8_t level = ((ptr[2] & 0x1) << 5) | ((ptr[3] >> 3) & 0x1f);
  const uint8_t rpu_present_flag = (ptr[3] >> 2) & 0x01;
  const uint8_t el_present_flag = (ptr[3] >> 1) & 0x01;
  const uint8_t bl_present_flag = (ptr[3] & 0x01);
  const int32_t bl_compatibility_id = (int32_t)(ptr[4] >> 4);

  LOG(LS_VERBOSE) << "profile-level-compatibility value in dv(c|v)c box "
                  << profile << "-" << level << "-" << bl_compatibility_id;

  // All Dolby Profiles will have profile and level info in MediaFormat
  // Profile 8 and 9 will have bl_compatibility_id too.
  const static Lookup<uint8_t, int32_t> profiles{
      {1, DolbyVisionProfileDvavPen},  {3, DolbyVisionProfileDvheDen},
      {4, DolbyVisionProfileDvheDtr},  {5, DolbyVisionProfileDvheStn},
      {6, DolbyVisionProfileDvheDth},  {7, DolbyVisionProfileDvheDtb},
      {8, DolbyVisionProfileDvheSt},   {9, DolbyVisionProfileDvavSe},
      {10, DolbyVisionProfileDvav110},
  };

  const static Lookup<uint8_t, int32_t> levels{
      {0, DolbyVisionLevelUnknown}, {1, DolbyVisionLevelHd24},
      {2, DolbyVisionLevelHd30},    {3, DolbyVisionLevelFhd24},
      {4, DolbyVisionLevelFhd30},   {5, DolbyVisionLevelFhd60},
      {6, DolbyVisionLevelUhd24},   {7, DolbyVisionLevelUhd30},
      {8, DolbyVisionLevelUhd48},   {9, DolbyVisionLevelUhd60},
      {10, DolbyVisionLevelUhd120}, {11, DolbyVisionLevel8k30},
      {12, DolbyVisionLevel8k60},
  };
  // set rpuAssoc
  if (rpu_present_flag && el_present_flag && !bl_present_flag) {
    format->setInt32("rpuAssoc", 1);
  }
  // set profile & level if they are recognized
  int32_t codecProfile;
  int32_t codecLevel;
  if (profiles.map(profile, &codecProfile)) {
    format->setInt32("profile", codecProfile);
    if (codecProfile == DolbyVisionProfileDvheSt ||
        codecProfile == DolbyVisionProfileDvavSe) {
      format->setInt32("bl_compatibility_id", bl_compatibility_id);
    }
    if (levels.map(level, &codecLevel)) {
      format->setInt32("level", codecLevel);
    }
  }
}

static void parseH263ProfileLevelFromD263(const uint8_t* ptr,
                                          size_t size,
                                          std::shared_ptr<Message>& format) {
  if (size < 7) {
    return;
  }

  const uint8_t profile = ptr[6];
  const uint8_t level = ptr[5];

  const static Lookup<uint8_t, int32_t> profiles{
      {0, H263ProfileBaseline},
      {1, H263ProfileH320Coding},
      {2, H263ProfileBackwardCompatible},
      {3, H263ProfileISWV2},
      {4, H263ProfileISWV3},
      {5, H263ProfileHighCompression},
      {6, H263ProfileInternet},
      {7, H263ProfileInterlace},
      {8, H263ProfileHighLatency},
  };

  const static Lookup<uint8_t, int32_t> levels{
      {10, H263Level10}, {20, H263Level20}, {30, H263Level30},
      {40, H263Level40}, {45, H263Level45}, {50, H263Level50},
      {60, H263Level60}, {70, H263Level70},
  };

  // set profile & level if they are recognized
  int32_t codecProfile;
  int32_t codecLevel;
  if (profiles.map(profile, &codecProfile)) {
    format->setInt32("profile", codecProfile);
    if (levels.map(level, &codecLevel)) {
      format->setInt32("level", codecLevel);
    }
  }
}

static void parseHevcProfileLevelFromHvcc(const uint8_t* ptr,
                                          size_t size,
                                          std::shared_ptr<Message>& format) {
  if (size < 13 || ptr[0] != 1) {  // configurationVersion == 1
    return;
  }

  const uint8_t profile = ptr[1] & 0x1F;
  const uint8_t tier = (ptr[1] & 0x20) >> 5;
  const uint8_t level = ptr[12];

  const static Lookup<std::pair<uint8_t, uint8_t>, int32_t> levels{
      {{0, 30}, HEVCMainTierLevel1},   {{0, 60}, HEVCMainTierLevel2},
      {{0, 63}, HEVCMainTierLevel21},  {{0, 90}, HEVCMainTierLevel3},
      {{0, 93}, HEVCMainTierLevel31},  {{0, 120}, HEVCMainTierLevel4},
      {{0, 123}, HEVCMainTierLevel41}, {{0, 150}, HEVCMainTierLevel5},
      {{0, 153}, HEVCMainTierLevel51}, {{0, 156}, HEVCMainTierLevel52},
      {{0, 180}, HEVCMainTierLevel6},  {{0, 183}, HEVCMainTierLevel61},
      {{0, 186}, HEVCMainTierLevel62}, {{1, 30}, HEVCHighTierLevel1},
      {{1, 60}, HEVCHighTierLevel2},   {{1, 63}, HEVCHighTierLevel21},
      {{1, 90}, HEVCHighTierLevel3},   {{1, 93}, HEVCHighTierLevel31},
      {{1, 120}, HEVCHighTierLevel4},  {{1, 123}, HEVCHighTierLevel41},
      {{1, 150}, HEVCHighTierLevel5},  {{1, 153}, HEVCHighTierLevel51},
      {{1, 156}, HEVCHighTierLevel52}, {{1, 180}, HEVCHighTierLevel6},
      {{1, 183}, HEVCHighTierLevel61}, {{1, 186}, HEVCHighTierLevel62},
  };

  const static Lookup<uint8_t, int32_t> profiles{
      {1, HEVCProfileMain},
      {2, HEVCProfileMain10},
      // use Main for Main Still Picture decoding
      {3, HEVCProfileMain},
  };

  // set profile & level if they are recognized
  int32_t codecProfile;
  int32_t codecLevel;
  if (!profiles.map(profile, &codecProfile)) {
    if (ptr[2] & 0x40 /* general compatibility flag 1 */) {
      // Note that this case covers Main Still Picture too
      codecProfile = HEVCProfileMain;
    } else if (ptr[2] & 0x20 /* general compatibility flag 2 */) {
      codecProfile = HEVCProfileMain10;
    } else {
      return;
    }
  }

  // bump to HDR profile
  if (isHdr(format) && codecProfile == HEVCProfileMain10) {
    codecProfile = HEVCProfileMain10HDR10;
  }

  format->setInt32("profile", codecProfile);
  if (levels.map(std::make_pair(tier, level), &codecLevel)) {
    format->setInt32("level", codecLevel);
  }
}

static void parseMpeg2ProfileLevelFromHeader(const uint8_t* data,
                                             size_t size,
                                             std::shared_ptr<Message>& format) {
  // find sequence extension
  const uint8_t* seq =
      (const uint8_t*)memmem(data, size, "\x00\x00\x01\xB5", 4);
  if (seq != NULL && seq + 5 < data + size) {
    const uint8_t start_code = seq[4] >> 4;
    if (start_code != 1 /* sequence extension ID */) {
      return;
    }
    const uint8_t indication = ((seq[4] & 0xF) << 4) | ((seq[5] & 0xF0) >> 4);

    const static Lookup<uint8_t, int32_t> profiles{
        {0x50, MPEG2ProfileSimple}, {0x40, MPEG2ProfileMain},
        {0x30, MPEG2ProfileSNR},    {0x20, MPEG2ProfileSpatial},
        {0x10, MPEG2ProfileHigh},
    };

    const static Lookup<uint8_t, int32_t> levels{
        {0x0A, MPEG2LevelLL}, {0x08, MPEG2LevelML}, {0x06, MPEG2LevelH14},
        {0x04, MPEG2LevelHL}, {0x02, MPEG2LevelHP},
    };

    const static Lookup<uint8_t, std::pair<int32_t, int32_t>> escapes{
        /* unsupported
        { 0x8E, { XXX_MPEG2ProfileMultiView, MPEG2LevelLL  } },
        { 0x8D, { XXX_MPEG2ProfileMultiView, MPEG2LevelML  } },
        { 0x8B, { XXX_MPEG2ProfileMultiView, MPEG2LevelH14 } },
        { 0x8A, { XXX_MPEG2ProfileMultiView, MPEG2LevelHL  } }, */
        {0x85, {MPEG2Profile422, MPEG2LevelML}},
        {0x82, {MPEG2Profile422, MPEG2LevelHL}},
    };

    int32_t profile;
    int32_t level;
    std::pair<int32_t, int32_t> profileLevel;
    if (escapes.map(indication, &profileLevel)) {
      format->setInt32("profile", profileLevel.first);
      format->setInt32("level", profileLevel.second);
    } else if (profiles.map(indication & 0x70, &profile)) {
      format->setInt32("profile", profile);
      if (levels.map(indication & 0xF, &level)) {
        format->setInt32("level", level);
      }
    }
  }
}

static void parseMpeg2ProfileLevelFromEsds(ESDS& esds,
                                           std::shared_ptr<Message>& format) {
  // esds seems to only contain the profile for MPEG-2
  uint8_t objType;
  if (esds.getObjectTypeIndication(&objType) == OK) {
    const static Lookup<uint8_t, int32_t> profiles{
        {0x60, MPEG2ProfileSimple}, {0x61, MPEG2ProfileMain},
        {0x62, MPEG2ProfileSNR},    {0x63, MPEG2ProfileSpatial},
        {0x64, MPEG2ProfileHigh},   {0x65, MPEG2Profile422},
    };

    int32_t profile;
    if (profiles.map(objType, &profile)) {
      format->setInt32("profile", profile);
    }
  }
}

static void parseMpeg4ProfileLevelFromCsd(const std::shared_ptr<Buffer>& csd,
                                          std::shared_ptr<Message>& format) {
  const uint8_t* data = csd->data();
  // find visual object sequence
  const uint8_t* seq =
      (const uint8_t*)memmem(data, csd->size(), "\x00\x00\x01\xB0", 4);
  if (seq != NULL && seq + 4 < data + csd->size()) {
    const uint8_t indication = seq[4];

    const static Lookup<uint8_t, std::pair<int32_t, int32_t>> table{
        {0b00000001, {MPEG4ProfileSimple, MPEG4Level1}},
        {0b00000010, {MPEG4ProfileSimple, MPEG4Level2}},
        {0b00000011, {MPEG4ProfileSimple, MPEG4Level3}},
        {0b00000100, {MPEG4ProfileSimple, MPEG4Level4a}},
        {0b00000101, {MPEG4ProfileSimple, MPEG4Level5}},
        {0b00000110, {MPEG4ProfileSimple, MPEG4Level6}},
        {0b00001000, {MPEG4ProfileSimple, MPEG4Level0}},
        {0b00001001, {MPEG4ProfileSimple, MPEG4Level0b}},
        {0b00010000, {MPEG4ProfileSimpleScalable, MPEG4Level0}},
        {0b00010001, {MPEG4ProfileSimpleScalable, MPEG4Level1}},
        {0b00010010, {MPEG4ProfileSimpleScalable, MPEG4Level2}},
        /* unsupported
        { 0b00011101, { XXX_MPEG4ProfileSimpleScalableER,        MPEG4Level0
        } }, { 0b00011110, { XXX_MPEG4ProfileSimpleScalableER, MPEG4Level1
        } }, { 0b00011111, { XXX_MPEG4ProfileSimpleScalableER, MPEG4Level2
        } }, */
        {0b00100001, {MPEG4ProfileCore, MPEG4Level1}},
        {0b00100010, {MPEG4ProfileCore, MPEG4Level2}},
        {0b00110010, {MPEG4ProfileMain, MPEG4Level2}},
        {0b00110011, {MPEG4ProfileMain, MPEG4Level3}},
        {0b00110100, {MPEG4ProfileMain, MPEG4Level4}},
        /* deprecated
        { 0b01000010, { MPEG4ProfileNbit,              MPEG4Level2  } }, */
        {0b01010001, {MPEG4ProfileScalableTexture, MPEG4Level1}},
        {0b01100001, {MPEG4ProfileSimpleFace, MPEG4Level1}},
        {0b01100010, {MPEG4ProfileSimpleFace, MPEG4Level2}},
        {0b01100011, {MPEG4ProfileSimpleFBA, MPEG4Level1}},
        {0b01100100, {MPEG4ProfileSimpleFBA, MPEG4Level2}},
        {0b01110001, {MPEG4ProfileBasicAnimated, MPEG4Level1}},
        {0b01110010, {MPEG4ProfileBasicAnimated, MPEG4Level2}},
        {0b10000001, {MPEG4ProfileHybrid, MPEG4Level1}},
        {0b10000010, {MPEG4ProfileHybrid, MPEG4Level2}},
        {0b10010001, {MPEG4ProfileAdvancedRealTime, MPEG4Level1}},
        {0b10010010, {MPEG4ProfileAdvancedRealTime, MPEG4Level2}},
        {0b10010011, {MPEG4ProfileAdvancedRealTime, MPEG4Level3}},
        {0b10010100, {MPEG4ProfileAdvancedRealTime, MPEG4Level4}},
        {0b10100001, {MPEG4ProfileCoreScalable, MPEG4Level1}},
        {0b10100010, {MPEG4ProfileCoreScalable, MPEG4Level2}},
        {0b10100011, {MPEG4ProfileCoreScalable, MPEG4Level3}},
        {0b10110001, {MPEG4ProfileAdvancedCoding, MPEG4Level1}},
        {0b10110010, {MPEG4ProfileAdvancedCoding, MPEG4Level2}},
        {0b10110011, {MPEG4ProfileAdvancedCoding, MPEG4Level3}},
        {0b10110100, {MPEG4ProfileAdvancedCoding, MPEG4Level4}},
        {0b11000001, {MPEG4ProfileAdvancedCore, MPEG4Level1}},
        {0b11000010, {MPEG4ProfileAdvancedCore, MPEG4Level2}},
        {0b11010001, {MPEG4ProfileAdvancedScalable, MPEG4Level1}},
        {0b11010010, {MPEG4ProfileAdvancedScalable, MPEG4Level2}},
        {0b11010011, {MPEG4ProfileAdvancedScalable, MPEG4Level3}},
        /* unsupported
        { 0b11100001, { XXX_MPEG4ProfileSimpleStudio,            MPEG4Level1
        } }, { 0b11100010, { XXX_MPEG4ProfileSimpleStudio, MPEG4Level2  } },
        { 0b11100011, { XXX_MPEG4ProfileSimpleStudio,            MPEG4Level3
        } }, { 0b11100100, { XXX_MPEG4ProfileSimpleStudio, MPEG4Level4  } },
        { 0b11100101, { XXX_MPEG4ProfileCoreStudio,              MPEG4Level1
        } }, { 0b11100110, { XXX_MPEG4ProfileCoreStudio, MPEG4Level2  } },
        { 0b11100111, { XXX_MPEG4ProfileCoreStudio,              MPEG4Level3
        } }, { 0b11101000, { XXX_MPEG4ProfileCoreStudio, MPEG4Level4  } },
        { 0b11101011, { XXX_MPEG4ProfileSimpleStudio,            MPEG4Level5
        } }, { 0b11101100, { XXX_MPEG4ProfileSimpleStudio, MPEG4Level6  }
        }, */
        {0b11110000, {MPEG4ProfileAdvancedSimple, MPEG4Level0}},
        {0b11110001, {MPEG4ProfileAdvancedSimple, MPEG4Level1}},
        {0b11110010, {MPEG4ProfileAdvancedSimple, MPEG4Level2}},
        {0b11110011, {MPEG4ProfileAdvancedSimple, MPEG4Level3}},
        {0b11110100, {MPEG4ProfileAdvancedSimple, MPEG4Level4}},
        {0b11110101, {MPEG4ProfileAdvancedSimple, MPEG4Level5}},
        {0b11110111, {MPEG4ProfileAdvancedSimple, MPEG4Level3b}},
        /* deprecated
        { 0b11111000, { XXX_MPEG4ProfileFineGranularityScalable, MPEG4Level0
        } }, { 0b11111001, { XXX_MPEG4ProfileFineGranularityScalable,
        MPEG4Level1  } }, { 0b11111010, {
        XXX_MPEG4ProfileFineGranularityScalable, MPEG4Level2  } }, {
        0b11111011, { XXX_MPEG4ProfileFineGranularityScalable, MPEG4Level3
        } }, { 0b11111100, { XXX_MPEG4ProfileFineGranularityScalable,
        MPEG4Level4  } }, { 0b11111101, {
        XXX_MPEG4ProfileFineGranularityScalable, MPEG4Level5  } }, */
    };

    std::pair<int32_t, int32_t> profileLevel;
    if (table.map(indication, &profileLevel)) {
      format->setInt32("profile", profileLevel.first);
      format->setInt32("level", profileLevel.second);
    }
  }
}

static void parseVp9ProfileLevelFromCsd(const std::shared_ptr<Buffer>& csd,
                                        std::shared_ptr<Message>& format) {
  const uint8_t* data = csd->data();
  size_t remaining = csd->size();

  while (remaining >= 2) {
    const uint8_t id = data[0];
    const uint8_t length = data[1];
    remaining -= 2;
    data += 2;
    if (length > remaining) {
      break;
    }
    switch (id) {
      case 1 /* profileId */:
        if (length >= 1) {
          const static Lookup<uint8_t, int32_t> profiles{
              {0, VP9Profile0},
              {1, VP9Profile1},
              {2, VP9Profile2},
              {3, VP9Profile3},
          };

          const static Lookup<int32_t, int32_t> toHdr{
              {VP9Profile2, VP9Profile2HDR},
              {VP9Profile3, VP9Profile3HDR},
          };

          int32_t profile;
          if (profiles.map(data[0], &profile)) {
            // convert to HDR profile
            if (isHdr(format)) {
              toHdr.lookup(profile, &profile);
            }

            format->setInt32("profile", profile);
          }
        }
        break;
      case 2 /* levelId */:
        if (length >= 1) {
          const static Lookup<uint8_t, int32_t> levels{
              {10, VP9Level1},  {11, VP9Level11}, {20, VP9Level2},
              {21, VP9Level21}, {30, VP9Level3},  {31, VP9Level31},
              {40, VP9Level4},  {41, VP9Level41}, {50, VP9Level5},
              {51, VP9Level51}, {52, VP9Level52}, {60, VP9Level6},
              {61, VP9Level61}, {62, VP9Level62},
          };

          int32_t level;
          if (levels.map(data[0], &level)) {
            format->setInt32("level", level);
          }
        }
        break;
      default:
        break;
    }
    remaining -= length;
    data += length;
  }
}

static void parseAV1ProfileLevelFromCsd(const std::shared_ptr<Buffer>& csd,
                                        std::shared_ptr<Message>& format) {
  // Parse CSD structure to extract profile level information
  // https://aomediacodec.github.io/av1-isobmff/#av1codecconfigurationbox
  const uint8_t* data = csd->data();
  size_t remaining = csd->size();
  if (remaining < 4 || data[0] != 0x81) {  // configurationVersion == 1
    return;
  }
  uint8_t profileData = (data[1] & 0xE0) >> 5;
  uint8_t levelData = data[1] & 0x1F;
  uint8_t highBitDepth = (data[2] & 0x40) >> 6;

  const static Lookup<std::pair<uint8_t, uint8_t>, int32_t> profiles{
      {{0, 0}, AV1ProfileMain8},
      {{1, 0}, AV1ProfileMain10},
  };

  int32_t profile;
  if (profiles.map(std::make_pair(highBitDepth, profileData), &profile)) {
    // bump to HDR profile
    if (isHdr(format) && profile == AV1ProfileMain10) {
      if (format->contains("hdr10-plus-info")) {
        profile = AV1ProfileMain10HDR10Plus;
      } else {
        profile = AV1ProfileMain10HDR10;
      }
    }
    format->setInt32("profile", profile);
  }
  const static Lookup<uint8_t, int32_t> levels{
      {0, AV1Level2},  {1, AV1Level21},  {2, AV1Level22},  {3, AV1Level23},
      {4, AV1Level3},  {5, AV1Level31},  {6, AV1Level32},  {7, AV1Level33},
      {8, AV1Level4},  {9, AV1Level41},  {10, AV1Level42}, {11, AV1Level43},
      {12, AV1Level5}, {13, AV1Level51}, {14, AV1Level52}, {15, AV1Level53},
      {16, AV1Level6}, {17, AV1Level61}, {18, AV1Level62}, {19, AV1Level63},
      {20, AV1Level7}, {21, AV1Level71}, {22, AV1Level72}, {23, AV1Level73},
  };

  int32_t level;
  if (levels.map(levelData, &level)) {
    format->setInt32("level", level);
  }
}

static std::vector<std::pair<const char*, uint32_t>> stringMappings{{
    {"album", kKeyAlbum},
    {"albumartist", kKeyAlbumArtist},
    {"artist", kKeyArtist},
    {"author", kKeyAuthor},
    {"cdtracknum", kKeyCDTrackNumber},
    {"compilation", kKeyCompilation},
    {"composer", kKeyComposer},
    {"date", kKeyDate},
    {"discnum", kKeyDiscNumber},
    {"genre", kKeyGenre},
    {"location", kKeyLocation},
    {"lyricist", kKeyWriter},
    {"manufacturer", kKeyManufacturer},
    {"title", kKeyTitle},
    {"year", kKeyYear},
}};

static std::vector<std::pair<const char*, uint32_t>> floatMappings{{
    {"capture-rate", kKeyCaptureFramerate},
}};

static std::vector<std::pair<const char*, uint32_t>> int64Mappings{{
    {"exif-offset", kKeyExifOffset},
    {"exif-size", kKeyExifSize},
    {"xmp-offset", kKeyXmpOffset},
    {"xmp-size", kKeyXmpSize},
    {"target-time", kKeyTargetTime},
    {"thumbnail-time", kKeyThumbnailTime},
    {"timeUs", kKeyTime},
    {"durationUs", kKeyDuration},
    {"sample-file-offset", kKeySampleFileOffset},
    {"last-sample-index-in-chunk", kKeyLastSampleIndexInChunk},
    {"sample-time-before-append", kKeySampleTimeBeforeAppend},
}};

static std::vector<std::pair<const char*, uint32_t>> int32Mappings{{
    {"loop", kKeyAutoLoop},
    {"time-scale", kKeyTimeScale},
    {"crypto-mode", kKeyCryptoMode},
    {"crypto-default-iv-size", kKeyCryptoDefaultIVSize},
    {"crypto-encrypted-byte-block", kKeyEncryptedByteBlock},
    {"crypto-skip-byte-block", kKeySkipByteBlock},
    {"frame-count", kKeyFrameCount},
    {"max-bitrate", kKeyMaxBitRate},
    {"pcm-big-endian", kKeyPcmBigEndian},
    {"temporal-layer-count", kKeyTemporalLayerCount},
    {"temporal-layer-id", kKeyTemporalLayerId},
    {"thumbnail-width", kKeyThumbnailWidth},
    {"thumbnail-height", kKeyThumbnailHeight},
    {"track-id", kKeyTrackID},
    {"valid-samples", kKeyValidSamples},
}};

static std::vector<std::pair<const char*, uint32_t>> bufferMappings{{
    {"albumart", kKeyAlbumArt},
    {"audio-presentation-info", kKeyAudioPresentationInfo},
    {"pssh", kKeyPssh},
    {"crypto-iv", kKeyCryptoIV},
    {"crypto-key", kKeyCryptoKey},
    {"crypto-encrypted-sizes", kKeyEncryptedSizes},
    {"crypto-plain-sizes", kKeyPlainSizes},
    {"icc-profile", kKeyIccProfile},
    {"sei", kKeySEI},
    {"text-format-data", kKeyTextFormatData},
    {"thumbnail-csd-hevc", kKeyThumbnailHVCC},
    {"slow-motion-markers", kKeySlowMotionMarkers},
    {"thumbnail-csd-av1c", kKeyThumbnailAV1C},
}};

static std::vector<std::pair<const char*, uint32_t>> CSDMappings{{
    {"csd-0", kKeyOpaqueCSD0},
    {"csd-1", kKeyOpaqueCSD1},
    {"csd-2", kKeyOpaqueCSD2},
}};

void convertMessageToMetaDataFromMappings(const std::shared_ptr<Message>& msg,
                                          std::shared_ptr<MetaData>& meta) {
  for (auto elem : stringMappings) {
    std::string value;
    if (msg->findString(elem.first, value)) {
      meta->setCString(elem.second, value.c_str());
    }
  }

  for (auto elem : floatMappings) {
    float value;
    if (msg->findFloat(elem.first, &value)) {
      meta->setFloat(elem.second, value);
    }
  }

  for (auto elem : int64Mappings) {
    int64_t value;
    if (msg->findInt64(elem.first, &value)) {
      meta->setInt64(elem.second, value);
    }
  }

  for (auto elem : int32Mappings) {
    int32_t value;
    if (msg->findInt32(elem.first, &value)) {
      meta->setInt32(elem.second, value);
    }
  }

  for (auto elem : bufferMappings) {
    std::shared_ptr<Buffer> value;
    if (msg->findBuffer(elem.first, value)) {
      meta->setData(elem.second, MetaData::Type::TYPE_NONE, value->data(),
                    value->size());
    }
  }

  for (auto elem : CSDMappings) {
    std::shared_ptr<Buffer> value;
    if (msg->findBuffer(elem.first, value)) {
      meta->setData(elem.second, MetaData::Type::TYPE_NONE, value->data(),
                    value->size());
    }
  }
}

void convertMetaDataToMessageFromMappings(const MetaData* meta,
                                          std::shared_ptr<Message>& format) {
  for (auto elem : stringMappings) {
    const char* value;
    if (meta->findCString(elem.second, &value)) {
      format->setString(elem.first, value, strlen(value));
    }
  }

  for (auto elem : floatMappings) {
    float value;
    if (meta->findFloat(elem.second, &value)) {
      format->setFloat(elem.first, value);
    }
  }

  for (auto elem : int64Mappings) {
    int64_t value;
    if (meta->findInt64(elem.second, &value)) {
      format->setInt64(elem.first, value);
    }
  }

  for (auto elem : int32Mappings) {
    int32_t value;
    if (meta->findInt32(elem.second, &value)) {
      format->setInt32(elem.first, value);
    }
  }

  for (auto elem : bufferMappings) {
    uint32_t type;
    const void* data;
    size_t size;
    if (meta->findData(elem.second, &type, &data, &size)) {
      std::shared_ptr<Buffer> buf = Buffer::CreateAsCopy(data, size);
      format->setBuffer(elem.first, buf);
    }
  }

  for (auto elem : CSDMappings) {
    uint32_t type;
    const void* data;
    size_t size;
    if (meta->findData(elem.second, &type, &data, &size)) {
      std::shared_ptr<Buffer> buf = Buffer::CreateAsCopy(data, size);
      buf->meta()->setInt32("csd", true);
      buf->meta()->setInt64("timeUs", 0);
      format->setBuffer(elem.first, buf);
    }
  }
}

status_t convertMetaDataToMessage(const MetaData* meta,
                                  std::shared_ptr<Message>& format) {
  if (format.get() != nullptr) {
    format->clear();
  }
  if (meta == nullptr) {
    LOG(LS_ERROR) << "convertMetaDataToMessage: nullptr input";
    return BAD_VALUE;
  }

  const char* mime;
  if (!meta->findCString(kKeyMIMEType, &mime)) {
    return BAD_VALUE;
  }

  int32_t codecType;
  if (!meta->findInt32(kKeyCodecType, &codecType)) {
    return BAD_VALUE;
  }

  auto msg = std::make_shared<Message>();
  msg->setString("mime", mime);

  msg->setInt32("codec", codecType);
  uint32_t ffType;
  const void* ffExData;
  size_t ffExsize;
  if (meta->findData(kKeyFFmpegExtraData, &ffType, &ffExData, &ffExsize)) {
    auto buffer = Buffer::CreateAsCopy(ffExData, ffExsize);
    msg->setBuffer("ffmpeg-exdata", buffer);
    std::shared_ptr<Buffer> buf;
    DCHECK(msg->findBuffer("ffmpeg-exdata", buf));
  }

  convertMetaDataToMessageFromMappings(meta, msg);

  uint32_t type;
  const void* data;
  size_t size;
  if (meta->findData(kKeyCASessionID, &type, &data, &size)) {
    auto buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }

    msg->setBuffer("ca-session-id", buffer);
    memcpy(buffer->data(), data, size);
  }

  if (meta->findData(kKeyCAPrivateData, &type, &data, &size)) {
    auto buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }

    msg->setBuffer("ca-private-data", buffer);
    memcpy(buffer->data(), data, size);
  }

  int32_t systemId;
  if (meta->findInt32(kKeyCASystemID, &systemId)) {
    msg->setInt32("ca-system-id", systemId);
  }

  if (!strncasecmp("video/scrambled", mime, 15) ||
      !strncasecmp("audio/scrambled", mime, 15)) {
    format = msg;
    return OK;
  }

  int64_t durationUs;
  if (meta->findInt64(kKeyDuration, &durationUs)) {
    msg->setInt64("durationUs", durationUs);
  }

  int32_t avgBitRate = 0;
  if (meta->findInt32(kKeyBitRate, &avgBitRate) && avgBitRate > 0) {
    msg->setInt32("bitrate", avgBitRate);
  }

  int32_t maxBitRate;
  if (meta->findInt32(kKeyMaxBitRate, &maxBitRate) && maxBitRate > 0 &&
      maxBitRate >= avgBitRate) {
    msg->setInt32("max-bitrate", maxBitRate);
  }

  int32_t isSync;
  if (meta->findInt32(kKeyIsSyncFrame, &isSync) && isSync != 0) {
    msg->setInt32("is-sync-frame", 1);
  }

  const char* lang;
  if (meta->findCString(kKeyMediaLanguage, &lang)) {
    msg->setString("language", lang);
  }

  if (!strncasecmp("video/", mime, 6) || !strncasecmp("image/", mime, 6)) {
    int32_t width, height;
    if (!meta->findInt32(kKeyWidth, &width) ||
        !meta->findInt32(kKeyHeight, &height)) {
      return BAD_VALUE;
    }

    msg->setInt32("width", width);
    msg->setInt32("height", height);

    int32_t displayWidth, displayHeight;
    if (meta->findInt32(kKeyDisplayWidth, &displayWidth) &&
        meta->findInt32(kKeyDisplayHeight, &displayHeight)) {
      msg->setInt32("display-width", displayWidth);
      msg->setInt32("display-height", displayHeight);
    }

    int32_t sarWidth, sarHeight;
    if (meta->findInt32(kKeySARWidth, &sarWidth) &&
        meta->findInt32(kKeySARHeight, &sarHeight)) {
      msg->setInt32("sar-width", sarWidth);
      msg->setInt32("sar-height", sarHeight);
    }

    if (!strncasecmp("image/", mime, 6)) {
      int32_t tileWidth, tileHeight, gridRows, gridCols;
      if (meta->findInt32(kKeyTileWidth, &tileWidth) &&
          meta->findInt32(kKeyTileHeight, &tileHeight) &&
          meta->findInt32(kKeyGridRows, &gridRows) &&
          meta->findInt32(kKeyGridCols, &gridCols)) {
        msg->setInt32("tile-width", tileWidth);
        msg->setInt32("tile-height", tileHeight);
        msg->setInt32("grid-rows", gridRows);
        msg->setInt32("grid-cols", gridCols);
      }
      int32_t isPrimary;
      if (meta->findInt32(kKeyTrackIsDefault, &isPrimary) && isPrimary) {
        msg->setInt32("is-default", 1);
      }
    }

    int32_t colorFormat;
    if (meta->findInt32(kKeyColorFormat, &colorFormat)) {
      msg->setInt32("color-format", colorFormat);
    }

    int32_t cropLeft, cropTop, cropRight, cropBottom;
    if (meta->findRect(kKeyCropRect, &cropLeft, &cropTop, &cropRight,
                       &cropBottom)) {
      msg->setRect("crop", cropLeft, cropTop, cropRight, cropBottom);
    }

    int32_t rotationDegrees;
    if (meta->findInt32(kKeyRotation, &rotationDegrees)) {
      msg->setInt32("rotation-degrees", rotationDegrees);
    }

    uint32_t type;
    const void* data;
    size_t size;
    if (meta->findData(kKeyHdrStaticInfo, &type, &data, &size) &&
        type == 'hdrS' && size == sizeof(HDRStaticInfo)) {
      ColorUtils::setHDRStaticInfoIntoFormat(*(HDRStaticInfo*)data, msg);
    }

    if (meta->findData(kKeyHdr10PlusInfo, &type, &data, &size) && size > 0) {
      auto buffer = std::make_shared<Buffer>(size);
      if (buffer.get() == nullptr || buffer->base() == nullptr) {
        return NO_MEMORY;
      }
      memcpy(buffer->data(), data, size);
      msg->setBuffer("hdr10-plus-info", buffer);
    }

    convertMetaDataToMessageColorAspects(meta, msg);
  } else if (!strncasecmp("audio/", mime, 6)) {
    int32_t numChannels, sampleRate;
    if (!meta->findInt32(kKeyChannelCount, &numChannels) ||
        !meta->findInt32(kKeySampleRate, &sampleRate)) {
      return BAD_VALUE;
    }

    msg->setInt32("channel-count", numChannels);
    msg->setInt32("sample-rate", sampleRate);

    int32_t bitsPerSample;
    if (meta->findInt32(kKeyBitsPerSample, &bitsPerSample)) {
      msg->setInt32("bits-per-sample", bitsPerSample);
    }

    int32_t channelMask;
    if (meta->findInt32(kKeyChannelMask, &channelMask)) {
      msg->setInt32("channel-mask", channelMask);
    }

    int32_t delay = 0;
    if (meta->findInt32(kKeyEncoderDelay, &delay)) {
      msg->setInt32("encoder-delay", delay);
    }
    int32_t padding = 0;
    if (meta->findInt32(kKeyEncoderPadding, &padding)) {
      msg->setInt32("encoder-padding", padding);
    }

    int32_t isADTS;
    if (meta->findInt32(kKeyIsADTS, &isADTS)) {
      msg->setInt32("is-adts", isADTS);
    }

    // TODO(youfa) add mpeg profile here
    //    int32_t mpeghProfileLevelIndication;
    //    if (meta->findInt32(kKeyMpeghProfileLevelIndication,
    //                        &mpeghProfileLevelIndication)) {
    //      msg->setInt32(AMEDIAFORMAT_KEY_MPEGH_PROFILE_LEVEL_INDICATION,
    //                    mpeghProfileLevelIndication);
    //    }
    //    int32_t mpeghReferenceChannelLayout;
    //    if (meta->findInt32(kKeyMpeghReferenceChannelLayout,
    //                        &mpeghReferenceChannelLayout)) {
    //      msg->setInt32(AMEDIAFORMAT_KEY_MPEGH_REFERENCE_CHANNEL_LAYOUT,
    //                    mpeghReferenceChannelLayout);
    //    }
    //    if (meta->findData(kKeyMpeghCompatibleSets, &type, &data, &size)) {
    //      auto buffer = std::make_shared<Buffer>(size);
    //      if (buffer.get() == nullptr || buffer->base() == nullptr) {
    //        return NO_MEMORY;
    //      }
    //      msg->setBuffer(AMEDIAFORMAT_KEY_MPEGH_COMPATIBLE_SETS, buffer);
    //      memcpy(buffer->data(), data, size);
    //    }

    int32_t aacProfile = -1;
    if (meta->findInt32(kKeyAACAOT, &aacProfile)) {
      msg->setInt32("aac-profile", aacProfile);
    }

    int32_t pcmEncoding;
    if (meta->findInt32(kKeyPcmEncoding, &pcmEncoding)) {
      msg->setInt32("pcm-encoding", pcmEncoding);
    }

    int32_t hapticChannelCount;
    if (meta->findInt32(kKeyHapticChannelCount, &hapticChannelCount)) {
      msg->setInt32("haptic-channel-count", hapticChannelCount);
    }
  }

  int32_t maxInputSize;
  if (meta->findInt32(kKeyMaxInputSize, &maxInputSize)) {
    msg->setInt32("max-input-size", maxInputSize);
  }

  int32_t maxWidth;
  if (meta->findInt32(kKeyMaxWidth, &maxWidth)) {
    msg->setInt32("max-width", maxWidth);
  }

  int32_t maxHeight;
  if (meta->findInt32(kKeyMaxHeight, &maxHeight)) {
    msg->setInt32("max-height", maxHeight);
  }

  int32_t rotationDegrees;
  if (meta->findInt32(kKeyRotation, &rotationDegrees)) {
    msg->setInt32("rotation-degrees", rotationDegrees);
  }

  int32_t fps;
  if (meta->findInt32(kKeyFrameRate, &fps) && fps > 0) {
    msg->setInt32("frame-rate", fps);
  }

  if (meta->findData(kKeyAVCC, &type, &data, &size)) {
    // Parse the AVCDecoderConfigurationRecord

    const uint8_t* ptr = (const uint8_t*)data;

    if (size < 7 || ptr[0] != 1) {  // configurationVersion == 1
      LOG(LS_ERROR) << "b/23680780";
      return BAD_VALUE;
    }

    parseAvcProfileLevelFromAvcc(ptr, size, msg);

    // There is decodable content out there that fails the following
    // assertion, let's be lenient for now...
    // CHECK((ptr[4] >> 2) == 0x3f);  // reserved

    // we can get lengthSize value from 1 + (ptr[4] & 3)

    // commented out check below as H264_QVGA_500_NO_AUDIO.3gp
    // violates it...
    // CHECK((ptr[5] >> 5) == 7);  // reserved

    size_t numSeqParameterSets = ptr[5] & 31;

    ptr += 6;
    size -= 6;

    auto buffer = std::make_shared<Buffer>(1024);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    buffer->setRange(0, 0);

    for (size_t i = 0; i < numSeqParameterSets; ++i) {
      if (size < 2) {
        LOG(LS_ERROR) << "b/23680780";
        return BAD_VALUE;
      }
      size_t length = U16_AT(ptr);

      ptr += 2;
      size -= 2;

      if (size < length) {
        return BAD_VALUE;
      }
      status_t err = copyNALUToBuffer(buffer, ptr, length);
      if (err != OK) {
        return err;
      }

      ptr += length;
      size -= length;
    }

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);

    msg->setBuffer("csd-0", buffer);

    buffer = std::make_shared<Buffer>(1024);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    buffer->setRange(0, 0);

    if (size < 1) {
      LOG(LS_ERROR) << "b/23680780";
      return BAD_VALUE;
    }
    size_t numPictureParameterSets = *ptr;
    ++ptr;
    --size;

    for (size_t i = 0; i < numPictureParameterSets; ++i) {
      if (size < 2) {
        LOG(LS_ERROR) << "b/23680780";
        return BAD_VALUE;
      }
      size_t length = U16_AT(ptr);

      ptr += 2;
      size -= 2;

      if (size < length) {
        return BAD_VALUE;
      }
      status_t err = copyNALUToBuffer(buffer, ptr, length);
      if (err != OK) {
        return err;
      }

      ptr += length;
      size -= length;
    }

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-1", buffer);
  } else if (meta->findData(kKeyHVCC, &type, &data, &size)) {
    const uint8_t* ptr = (const uint8_t*)data;

    if (size < 23 || (ptr[0] != 1 && ptr[0] != 0)) {
      // configurationVersion == 1 or 0
      // 1 is what the standard dictates, but some old muxers may have used 0.
      LOG(LS_ERROR) << "b/23680780";
      return BAD_VALUE;
    }

    const size_t dataSize = size;  // save for later
    ptr += 22;
    size -= 22;

    size_t numofArrays = (char)ptr[0];
    ptr += 1;
    size -= 1;
    size_t j = 0, i = 0;

    auto buffer = std::make_shared<Buffer>(1024);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    buffer->setRange(0, 0);

    HevcParameterSets hvcc;

    for (i = 0; i < numofArrays; i++) {
      if (size < 3) {
        LOG(LS_ERROR) << "b/23680780";
        return BAD_VALUE;
      }
      ptr += 1;
      size -= 1;

      // Num of nals
      size_t numofNals = U16_AT(ptr);

      ptr += 2;
      size -= 2;

      for (j = 0; j < numofNals; j++) {
        if (size < 2) {
          LOG(LS_ERROR) << "b/23680780";
          return BAD_VALUE;
        }
        size_t length = U16_AT(ptr);

        ptr += 2;
        size -= 2;

        if (size < length) {
          return BAD_VALUE;
        }
        status_t err = copyNALUToBuffer(buffer, ptr, length);
        if (err != OK) {
          return err;
        }
        (void)hvcc.addNalUnit(ptr, length);

        ptr += length;
        size -= length;
      }
    }
    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-0", buffer);

    // if we saw VUI color information we know whether this is HDR because VUI
    // trumps other format parameters for HEVC.
    HevcParameterSets::Info info = hvcc.getInfo();
    if (info & hvcc.kInfoHasColorDescription) {
      msg->setInt32("android._is-hdr", (info & hvcc.kInfoIsHdr) != 0);
    }

    uint32_t isoPrimaries, isoTransfer, isoMatrix, isoRange;
    if (hvcc.findParam32(kColourPrimaries, &isoPrimaries) &&
        hvcc.findParam32(kTransferCharacteristics, &isoTransfer) &&
        hvcc.findParam32(kMatrixCoeffs, &isoMatrix) &&
        hvcc.findParam32(kVideoFullRangeFlag, &isoRange)) {
      LOG(LS_VERBOSE) << "found iso color aspects : primaris=" << isoPrimaries
                      << ", transfer=" << isoTransfer
                      << ", matrix=" << isoMatrix << ", range=" << isoRange;

      ColorAspects aspects;
      ColorUtils::convertIsoColorAspectsToCodecAspects(
          isoPrimaries, isoTransfer, isoMatrix, isoRange, aspects);

      if (aspects.mPrimaries == ColorAspects::PrimariesUnspecified) {
        int32_t primaries;
        if (meta->findInt32(kKeyColorPrimaries, &primaries)) {
          LOG(LS_VERBOSE) << "unspecified primaries found, replaced to "
                          << primaries;
          aspects.mPrimaries = static_cast<ColorAspects::Primaries>(primaries);
        }
      }
      if (aspects.mTransfer == ColorAspects::TransferUnspecified) {
        int32_t transferFunction;
        if (meta->findInt32(kKeyTransferFunction, &transferFunction)) {
          LOG(LS_VERBOSE) << "unspecified transfer found, replaced to "
                          << transferFunction;
          aspects.mTransfer =
              static_cast<ColorAspects::Transfer>(transferFunction);
        }
      }
      if (aspects.mMatrixCoeffs == ColorAspects::MatrixUnspecified) {
        int32_t colorMatrix;
        if (meta->findInt32(kKeyColorMatrix, &colorMatrix)) {
          LOG(LS_VERBOSE) << "unspecified matrix found, replaced to "
                          << colorMatrix;
          aspects.mMatrixCoeffs =
              static_cast<ColorAspects::MatrixCoeffs>(colorMatrix);
        }
      }
      if (aspects.mRange == ColorAspects::RangeUnspecified) {
        int32_t range;
        if (meta->findInt32(kKeyColorRange, &range)) {
          LOG(LS_VERBOSE) << "unspecified range found, replaced to " << range;
          aspects.mRange = static_cast<ColorAspects::Range>(range);
        }
      }

      int32_t standard, transfer, range;
      if (ColorUtils::convertCodecColorAspectsToPlatformAspects(
              aspects, &range, &standard, &transfer) == OK) {
        msg->setInt32("color-standard", standard);
        msg->setInt32("color-transfer", transfer);
        msg->setInt32("color-range", range);
      }
    }

    parseHevcProfileLevelFromHvcc((const uint8_t*)data, dataSize, msg);
  } else if (meta->findData(kKeyAV1C, &type, &data, &size)) {
    auto buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    memcpy(buffer->data(), data, size);

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-0", buffer);
    parseAV1ProfileLevelFromCsd(buffer, msg);
  } else if (meta->findData(kKeyESDS, &type, &data, &size)) {
    ESDS esds((const char*)data, size);
    if (esds.InitCheck() != (status_t)OK) {
      return BAD_VALUE;
    }

    const void* codec_specific_data;
    size_t codec_specific_data_size;
    esds.getCodecSpecificInfo(&codec_specific_data, &codec_specific_data_size);

    auto buffer = std::make_shared<Buffer>(codec_specific_data_size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }

    memcpy(buffer->data(), codec_specific_data, codec_specific_data_size);

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-0", buffer);

    if (!strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_MPEG4)) {
      parseMpeg4ProfileLevelFromCsd(buffer, msg);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_MPEG2)) {
      parseMpeg2ProfileLevelFromEsds(esds, msg);
      if (meta->findData(kKeyStreamHeader, &type, &data, &size)) {
        parseMpeg2ProfileLevelFromHeader((uint8_t*)data, size, msg);
      }
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC)) {
      parseAacProfileFromCsd(buffer, msg);
    }

    uint32_t maxBitrate, avgBitrate;
    if (esds.getBitRate(&maxBitrate, &avgBitrate) == OK) {
      if (!meta->hasData(kKeyBitRate) && avgBitrate > 0 &&
          avgBitrate <= INT32_MAX) {
        msg->setInt32("bitrate", (int32_t)avgBitrate);
      } else {
        (void)msg->findInt32("bitrate", (int32_t*)&avgBitrate);
      }
      if (!meta->hasData(kKeyMaxBitRate) && maxBitrate > 0 &&
          maxBitrate <= INT32_MAX && maxBitrate >= avgBitrate) {
        msg->setInt32("max-bitrate", (int32_t)maxBitrate);
      }
    }
  } else if (meta->findData(kKeyD263, &type, &data, &size)) {
    const uint8_t* ptr = (const uint8_t*)data;
    parseH263ProfileLevelFromD263(ptr, size, msg);
  } else if (meta->findData(kKeyOpusHeader, &type, &data, &size)) {
    auto buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    memcpy(buffer->data(), data, size);

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-0", buffer);

    if (!meta->findData(kKeyOpusCodecDelay, &type, &data, &size)) {
      return -EINVAL;
    }

    buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    memcpy(buffer->data(), data, size);

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-1", buffer);

    if (!meta->findData(kKeyOpusSeekPreRoll, &type, &data, &size)) {
      return -EINVAL;
    }

    buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    memcpy(buffer->data(), data, size);

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-2", buffer);
  } else if (meta->findData(kKeyVp9CodecPrivate, &type, &data, &size)) {
    auto buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    memcpy(buffer->data(), data, size);

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-0", buffer);

    parseVp9ProfileLevelFromCsd(buffer, msg);
  } else if (meta->findData(kKeyAlacMagicCookie, &type, &data, &size)) {
    LOG(LS_VERBOSE)
        << "convertMetaDataToMessage found kKeyAlacMagicCookie of size "
        << size;
    auto buffer = std::make_shared<Buffer>(size);
    if (buffer.get() == nullptr || buffer->base() == nullptr) {
      return NO_MEMORY;
    }
    memcpy(buffer->data(), data, size);

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-0", buffer);
  }

  if (meta->findData(kKeyDVCC, &type, &data, &size) ||
      meta->findData(kKeyDVVC, &type, &data, &size) ||
      meta->findData(kKeyDVWC, &type, &data, &size)) {
    std::shared_ptr<Buffer> buffer, csdOrg;
    if (msg->findBuffer("csd-0", csdOrg)) {
      auto buffer = std::make_shared<Buffer>(size + csdOrg->size());
      if (buffer.get() == nullptr || buffer->base() == nullptr) {
        return NO_MEMORY;
      }

      memcpy(buffer->data(), csdOrg->data(), csdOrg->size());
      memcpy(buffer->data() + csdOrg->size(), data, size);
    } else {
      auto buffer = std::make_shared<Buffer>(size);
      if (buffer.get() == nullptr || buffer->base() == nullptr) {
        return NO_MEMORY;
      }
      memcpy(buffer->data(), data, size);
    }

    buffer->meta()->setInt32("csd", true);
    buffer->meta()->setInt64("timeUs", 0);
    msg->setBuffer("csd-0", buffer);

    const uint8_t* ptr = (const uint8_t*)data;
    LOG(LS_VERBOSE)
        << "DV: calling parseDolbyVisionProfileLevelFromDvcc with data size "
        << size;
    parseDolbyVisionProfileLevelFromDvcc(ptr, size, msg);
  }

  format = msg;

  return OK;
  return OK;
}
status_t convertMetaDataToMessage(const std::shared_ptr<MetaData>& meta,
                                  std::shared_ptr<Message>& format) {
  return convertMetaDataToMessage(meta.get(), format);
}

status_t convertMessageToMetaData(const std::shared_ptr<Message>& format,
                                  std::shared_ptr<MetaData>& meta) {
  return OK;
}

} /* namespace avp */
