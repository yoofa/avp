/*
 * box_types.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_ISOBMFF_BOX_TYPES_H_
#define DEMUXER_ISOBMFF_BOX_TYPES_H_

#include <cstdint>

namespace ave {
namespace isobmff {

// Construct a FourCC from four characters at compile time.
constexpr uint32_t FourCC(char a, char b, char c, char d) {
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

// File-level boxes
constexpr uint32_t FOURCC_ftyp = FourCC('f', 't', 'y', 'p');
constexpr uint32_t FOURCC_moov = FourCC('m', 'o', 'o', 'v');
constexpr uint32_t FOURCC_mdat = FourCC('m', 'd', 'a', 't');
constexpr uint32_t FOURCC_free = FourCC('f', 'r', 'e', 'e');
constexpr uint32_t FOURCC_skip = FourCC('s', 'k', 'i', 'p');
constexpr uint32_t FOURCC_wide = FourCC('w', 'i', 'd', 'e');
constexpr uint32_t FOURCC_uuid = FourCC('u', 'u', 'i', 'd');

// Movie-level boxes (under moov)
constexpr uint32_t FOURCC_mvhd = FourCC('m', 'v', 'h', 'd');
constexpr uint32_t FOURCC_trak = FourCC('t', 'r', 'a', 'k');
constexpr uint32_t FOURCC_udta = FourCC('u', 'd', 't', 'a');
constexpr uint32_t FOURCC_meta = FourCC('m', 'e', 't', 'a');

// Track-level boxes (under trak)
constexpr uint32_t FOURCC_tkhd = FourCC('t', 'k', 'h', 'd');
constexpr uint32_t FOURCC_mdia = FourCC('m', 'd', 'i', 'a');
constexpr uint32_t FOURCC_edts = FourCC('e', 'd', 't', 's');
constexpr uint32_t FOURCC_elst = FourCC('e', 'l', 's', 't');

// Media-level boxes (under mdia)
constexpr uint32_t FOURCC_mdhd = FourCC('m', 'd', 'h', 'd');
constexpr uint32_t FOURCC_hdlr = FourCC('h', 'd', 'l', 'r');
constexpr uint32_t FOURCC_minf = FourCC('m', 'i', 'n', 'f');

// Media-info boxes (under minf)
constexpr uint32_t FOURCC_vmhd = FourCC('v', 'm', 'h', 'd');
constexpr uint32_t FOURCC_smhd = FourCC('s', 'm', 'h', 'd');
constexpr uint32_t FOURCC_dinf = FourCC('d', 'i', 'n', 'f');
constexpr uint32_t FOURCC_stbl = FourCC('s', 't', 'b', 'l');

// Sample Table boxes (under stbl)
constexpr uint32_t FOURCC_stsd = FourCC('s', 't', 's', 'd');
constexpr uint32_t FOURCC_stts = FourCC('s', 't', 't', 's');
constexpr uint32_t FOURCC_ctts = FourCC('c', 't', 't', 's');
constexpr uint32_t FOURCC_stsc = FourCC('s', 't', 's', 'c');
constexpr uint32_t FOURCC_stsz = FourCC('s', 't', 's', 'z');
constexpr uint32_t FOURCC_stz2 = FourCC('s', 't', 'z', '2');
constexpr uint32_t FOURCC_stco = FourCC('s', 't', 'c', 'o');
constexpr uint32_t FOURCC_co64 = FourCC('c', 'o', '6', '4');
constexpr uint32_t FOURCC_stss = FourCC('s', 't', 's', 's');

// Sample description entries
constexpr uint32_t FOURCC_avc1 = FourCC('a', 'v', 'c', '1');
constexpr uint32_t FOURCC_avc3 = FourCC('a', 'v', 'c', '3');
constexpr uint32_t FOURCC_avcC = FourCC('a', 'v', 'c', 'C');
constexpr uint32_t FOURCC_hvc1 = FourCC('h', 'v', 'c', '1');
constexpr uint32_t FOURCC_hev1 = FourCC('h', 'e', 'v', '1');
constexpr uint32_t FOURCC_hvcC = FourCC('h', 'v', 'c', 'C');
constexpr uint32_t FOURCC_vp09 = FourCC('v', 'p', '0', '9');
constexpr uint32_t FOURCC_vpcC = FourCC('v', 'p', 'c', 'C');
constexpr uint32_t FOURCC_av01 = FourCC('a', 'v', '0', '1');
constexpr uint32_t FOURCC_av1C = FourCC('a', 'v', '1', 'C');
constexpr uint32_t FOURCC_mp4a = FourCC('m', 'p', '4', 'a');
constexpr uint32_t FOURCC_mp4v = FourCC('m', 'p', '4', 'v');
constexpr uint32_t FOURCC_esds = FourCC('e', 's', 'd', 's');
constexpr uint32_t FOURCC_Opus = FourCC('O', 'p', 'u', 's');
constexpr uint32_t FOURCC_dOps = FourCC('d', 'O', 'p', 's');
constexpr uint32_t FOURCC_fLaC = FourCC('f', 'L', 'a', 'C');
constexpr uint32_t FOURCC_dfLa = FourCC('d', 'f', 'L', 'a');
constexpr uint32_t FOURCC_ac_3 = FourCC('a', 'c', '-', '3');
constexpr uint32_t FOURCC_ec_3 = FourCC('e', 'c', '-', '3');
constexpr uint32_t FOURCC_dac3 = FourCC('d', 'a', 'c', '3');
constexpr uint32_t FOURCC_dec3 = FourCC('d', 'e', 'c', '3');
constexpr uint32_t FOURCC_alac = FourCC('a', 'l', 'a', 'c');
constexpr uint32_t FOURCC_samr = FourCC('s', 'a', 'm', 'r');
constexpr uint32_t FOURCC_sawb = FourCC('s', 'a', 'w', 'b');
constexpr uint32_t FOURCC_tx3g = FourCC('t', 'x', '3', 'g');

// Handler types (from hdlr box)
constexpr uint32_t FOURCC_vide = FourCC('v', 'i', 'd', 'e');
constexpr uint32_t FOURCC_soun = FourCC('s', 'o', 'u', 'n');
constexpr uint32_t FOURCC_subt = FourCC('s', 'u', 'b', 't');
constexpr uint32_t FOURCC_text = FourCC('t', 'e', 'x', 't');

// Utility: check if a box type is a known container
inline bool IsContainerBox(uint32_t type) {
  switch (type) {
    case FOURCC_moov:
    case FOURCC_trak:
    case FOURCC_mdia:
    case FOURCC_minf:
    case FOURCC_stbl:
    case FOURCC_edts:
    case FOURCC_dinf:
    case FOURCC_udta:
      return true;
    default:
      return false;
  }
}

// Utility: convert FourCC to string for debugging
inline void FourCCToString(uint32_t fourcc, char out[5]) {
  out[0] = static_cast<char>((fourcc >> 24) & 0xff);
  out[1] = static_cast<char>((fourcc >> 16) & 0xff);
  out[2] = static_cast<char>((fourcc >> 8) & 0xff);
  out[3] = static_cast<char>(fourcc & 0xff);
  out[4] = '\0';
}

}  // namespace isobmff
}  // namespace ave

#endif  // DEMUXER_ISOBMFF_BOX_TYPES_H_
