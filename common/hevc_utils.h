/*
 * hevc_utils.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef HEVC_UTILS_H
#define HEVC_UTILS_H

#include <memory>
#include <unordered_map>
#include <vector>

#include "base/constructor_magic.h"
#include "base/types.h"
#include "common/buffer.h"

namespace avp {

enum {
  kHevcNalUnitTypeCodedSliceIdr = 19,
  kHevcNalUnitTypeCodedSliceIdrNoLP = 20,
  kHevcNalUnitTypeCodedSliceCra = 21,

  kHevcNalUnitTypeVps = 32,
  kHevcNalUnitTypeSps = 33,
  kHevcNalUnitTypePps = 34,
  kHevcNalUnitTypePrefixSei = 39,
  kHevcNalUnitTypeSuffixSei = 40,
};

enum {
  // uint8_t
  kGeneralProfileSpace,
  // uint8_t
  kGeneralTierFlag,
  // uint8_t
  kGeneralProfileIdc,
  // uint32_t
  kGeneralProfileCompatibilityFlags,
  // uint64_t
  kGeneralConstraintIndicatorFlags,
  // uint8_t
  kGeneralLevelIdc,
  // uint8_t
  kChromaFormatIdc,
  // uint8_t
  kBitDepthLumaMinus8,
  // uint8_t
  kBitDepthChromaMinus8,
  // uint8_t
  kVideoFullRangeFlag,
  // uint8_t
  kColourPrimaries,
  // uint8_t
  kTransferCharacteristics,
  // uint8_t
  kMatrixCoeffs,
};

class HevcParameterSets {
 public:
  enum Info : uint32_t {
    kInfoNone = 0,
    kInfoIsHdr = 1 << 0,
    kInfoHasColorDescription = 1 << 1,
  };

  HevcParameterSets();

  status_t addNalUnit(const uint8_t* data, size_t size);

  bool findParam8(uint32_t key, uint8_t* param);
  bool findParam16(uint32_t key, uint16_t* param);
  bool findParam32(uint32_t key, uint32_t* param);
  bool findParam64(uint32_t key, uint64_t* param);

  inline size_t getNumNalUnits() { return mNalUnits.size(); }
  size_t getNumNalUnitsOfType(uint8_t type);
  uint8_t getType(size_t index);
  size_t getSize(size_t index);
  // Note that this method does not write the start code.
  bool write(size_t index, uint8_t* dest, size_t size);
  status_t makeHvcc(uint8_t* hvcc, size_t* hvccSize, size_t nalSizeLength);
  void FindHEVCDimensions(const std::shared_ptr<Buffer>& SpsBuffer,
                          int32_t* width,
                          int32_t* height);

  Info getInfo() const { return mInfo; }
  static bool IsHevcIDR(const uint8_t* data, size_t size);

 private:
  status_t parseVps(const uint8_t* data, size_t size);
  status_t parseSps(const uint8_t* data, size_t size);
  status_t parsePps(const uint8_t* data, size_t size);

  // KeyedVector<uint32_t, uint64_t> mParams;
  std::unordered_map<uint32_t, uint64_t> mParams;
  std::vector<std::shared_ptr<Buffer>> mNalUnits;
  Info mInfo;

  AVP_DISALLOW_COPY_AND_ASSIGN(HevcParameterSets);
};
} /* namespace avp */

#endif /* !HEVC_UTILS_H */
