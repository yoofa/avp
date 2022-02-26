/*
 * avc_utils.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVC_UTILS_H
#define AVC_UTILS_H

#include "base/types.h"
#include "common/buffer.h"

namespace avp {

#define NELEM(x) ((int)(sizeof(x) / sizeof((x)[0])))

class BitReader;

enum {
  kAVCProfileBaseline = 0x42,
  kAVCProfileMain = 0x4d,
  kAVCProfileExtended = 0x58,
  kAVCProfileHigh = 0x64,
  kAVCProfileHigh10 = 0x6e,
  kAVCProfileHigh422 = 0x7a,
  kAVCProfileHigh444 = 0xf4,
  kAVCProfileCAVLC444Intra = 0x2c
};

struct NALPosition {
  uint32_t nalOffset;
  uint32_t nalSize;
};

// Optionally returns sample aspect ratio as well.
void FindAVCDimensions(const std::shared_ptr<Buffer>& seqParamSet,
                       int32_t* width,
                       int32_t* height,
                       int32_t* sarWidth = NULL,
                       int32_t* sarHeight = NULL);

// Gets and returns an unsigned exp-golomb (ue) value from a bit reader |br|.
// Aborts if the value is more than 64 bits long (>=0xFFFF (!)) or the bit
// reader overflows.
unsigned parseUE(BitReader* br);

// Gets and returns a signed exp-golomb (se) value from a bit reader |br|.
// Aborts if the value is more than 64 bits long (>0x7FFF || <-0x7FFF (!)) or
// the bit reader overflows.
signed parseSE(BitReader* br);

// Gets an unsigned exp-golomb (ue) value from a bit reader |br|, and returns it
// if it was successful. Returns |fallback| if it was unsuccessful. Note: if the
// value was longer that 64 bits, it reads past the value and still returns
// |fallback|.
unsigned parseUEWithFallback(BitReader* br, unsigned fallback);

// Gets a signed exp-golomb (se) value from a bit reader |br|, and returns it if
// it was successful. Returns |fallback| if it was unsuccessful. Note: if the
// value was longer that 64 bits, it reads past the value and still returns
// |fallback|.
signed parseSEWithFallback(BitReader* br, signed fallback);

// Skips an unsigned exp-golomb (ue) value from bit reader |br|.
inline void skipUE(BitReader* br) {
  (void)parseUEWithFallback(br, 0U);
}

// Skips a signed exp-golomb (se) value from bit reader |br|.
inline void skipSE(BitReader* br) {
  (void)parseSEWithFallback(br, 0);
}

status_t getNextNALUnit(const uint8_t** _data,
                        size_t* _size,
                        const uint8_t** nalStart,
                        size_t* nalSize,
                        bool startCodeFollows = false);

std::shared_ptr<Buffer> MakeAVCCodecSpecificData(
    const std::shared_ptr<Buffer>& accessUnit,
    int32_t* width,
    int32_t* height,
    int32_t* sarWidth = nullptr,
    int32_t* sarHeight = nullptr);

bool IsIDR(const uint8_t* data, size_t size);
bool IsAVCReferenceFrame(const std::shared_ptr<Buffer>& accessUnit);
uint32_t FindAVCLayerId(const uint8_t* data, size_t size);

const char* AVCProfileToString(uint8_t profile);

// Given an MPEG4 video VOL-header chunk (starting with 0x00 0x00 0x01 0x2?)
// parse it and fill in dimensions, returns true iff successful.
bool ExtractDimensionsFromVOLHeader(const uint8_t* data,
                                    size_t size,
                                    int32_t* width,
                                    int32_t* height);

bool GetMPEGAudioFrameSize(uint32_t header,
                           size_t* frame_size,
                           int* out_sampling_rate = NULL,
                           int* out_channels = NULL,
                           int* out_bitrate = NULL,
                           int* out_num_samples = NULL);
} /* namespace avp */

#endif /* !AVC_UTILS_H */
