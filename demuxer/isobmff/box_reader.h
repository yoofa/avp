/*
 * box_reader.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_ISOBMFF_BOX_READER_H_
#define DEMUXER_ISOBMFF_BOX_READER_H_

#include <cstdint>

#include "base/data_source/data_source_base.h"
#include "base/errors.h"

namespace ave {
namespace isobmff {

// Header of an ISO BMFF box (atom).
struct BoxHeader {
  uint32_t type;        // FourCC
  off64_t offset;       // File offset of the box start (first byte of size)
  off64_t size;         // Total box size including header (0 = extends to EOF)
  off64_t header_size;  // 8 or 16 bytes
  off64_t data_offset() const { return offset + header_size; }
  off64_t data_size() const { return size - header_size; }
};

// Read a box header at the given offset.
// Returns OK on success, or error (ERROR_IO, ERROR_MALFORMED).
status_t ReadBoxHeader(DataSourceBase* source,
                       off64_t offset,
                       BoxHeader* header);

// Read a full-box header (version + flags following the box header).
// |offset| should point to the byte after the box header (i.e. data_offset).
// Returns OK and fills version (uint8) and flags (uint32, 24-bit).
status_t ReadFullBoxHeader(DataSourceBase* source,
                           off64_t offset,
                           uint8_t* version,
                           uint32_t* flags);

}  // namespace isobmff
}  // namespace ave

#endif  // DEMUXER_ISOBMFF_BOX_READER_H_
