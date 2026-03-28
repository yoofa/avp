/*
 * box_reader.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "box_reader.h"

#include <array>

#include <arpa/inet.h>

#include "base/byte_utils.h"
#include "base/logging.h"
#include "media/foundation/media_errors.h"

namespace ave {
namespace isobmff {

using media::ERROR_IO;
using media::ERROR_MALFORMED;

status_t ReadBoxHeader(DataSourceBase* source,
                       off64_t offset,
                       BoxHeader* header) {
  std::array<uint8_t, 8> buf{};
  if (source->ReadAt(offset, buf.data(), 8) != 8) {
    return ERROR_IO;
  }

  uint32_t size32 = (static_cast<uint32_t>(buf[0]) << 24) |
                    (static_cast<uint32_t>(buf[1]) << 16) |
                    (static_cast<uint32_t>(buf[2]) << 8) |
                    static_cast<uint32_t>(buf[3]);

  uint32_t type = (static_cast<uint32_t>(buf[4]) << 24) |
                  (static_cast<uint32_t>(buf[5]) << 16) |
                  (static_cast<uint32_t>(buf[6]) << 8) |
                  static_cast<uint32_t>(buf[7]);

  header->offset = offset;
  header->type = type;

  if (size32 == 1) {
    // 64-bit extended size
    std::array<uint8_t, 8> buf64{};
    if (source->ReadAt(offset + 8, buf64.data(), 8) != 8) {
      return ERROR_IO;
    }
    uint64_t size64 = ntoh64(*reinterpret_cast<uint64_t*>(buf64.data()));
    if (size64 < 16) {
      AVE_LOG(LS_ERROR) << "Invalid extended box size: " << size64;
      return ERROR_MALFORMED;
    }
    header->size = static_cast<off64_t>(size64);
    header->header_size = 16;
  } else if (size32 == 0) {
    // Box extends to end of file
    off64_t file_size = 0;
    if (source->GetSize(&file_size) != OK) {
      return ERROR_IO;
    }
    header->size = file_size - offset;
    header->header_size = 8;
  } else {
    header->size = size32;
    header->header_size = 8;
  }

  if (header->size < header->header_size) {
    AVE_LOG(LS_ERROR) << "Box size " << header->size
                      << " less than header size " << header->header_size;
    return ERROR_MALFORMED;
  }

  return OK;
}

status_t ReadFullBoxHeader(DataSourceBase* source,
                           off64_t offset,
                           uint8_t* version,
                           uint32_t* flags) {
  std::array<uint8_t, 4> buf{};
  if (source->ReadAt(offset, buf.data(), 4) != 4) {
    return ERROR_IO;
  }

  *version = buf[0];
  *flags = (static_cast<uint32_t>(buf[1]) << 16) |
           (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);

  return OK;
}

}  // namespace isobmff
}  // namespace ave
