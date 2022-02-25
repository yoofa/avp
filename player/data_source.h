/*
 * data_source.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include <fcntl.h>

#include "base/byte_utils.h"
#include "base/types.h"

namespace avp {

class DataSource {
 public:
  enum Flags {
    kWantsPrefetching = 1,
    kStreamedFromLocalHost = 2,
    kIsCachingDataSource = 4,
    kIsHTTPBasedSource = 8,
    kIsLocalFileSource = 16,

    kSeekable = 32,
  };

  DataSource() {}
  virtual ~DataSource() = default;

  virtual status_t initCheck() const = 0;

  // read from current offset
  virtual ssize_t read(void* data, size_t size) = 0;

  // read from start offset
  virtual ssize_t readAt(off64_t offset, void* data, size_t size) = 0;

  virtual status_t getPosition(off64_t* position) = 0;

  virtual ssize_t seek(off64_t position, int whence) = 0;

  // Convenience methods:
  bool getUInt16(off64_t offset, uint16_t* x) {
    *x = 0;

    uint8_t byte[2];
    if (readAt(offset, byte, 2) != 2) {
      return false;
    }

    *x = (byte[0] << 8) | byte[1];

    return true;
  }
  // 3 byte int, returned as a 32-bit int
  bool getUInt24(off64_t offset, uint32_t* x) {
    *x = 0;

    uint8_t byte[3];
    if (readAt(offset, byte, 3) != 3) {
      return false;
    }

    *x = (byte[0] << 16) | (byte[1] << 8) | byte[2];

    return true;
  }
  bool getUInt32(off64_t offset, uint32_t* x) {
    *x = 0;

    uint32_t tmp;
    if (readAt(offset, &tmp, 4) != 4) {
      return false;
    }

    *x = ntohl(tmp);

    return true;
  }
  bool getUInt64(off64_t offset, uint64_t* x) {
    *x = 0;

    uint64_t tmp;
    if (readAt(offset, &tmp, 8) != 8) {
      return false;
    }

    *x = ntoh64(tmp);

    return true;
  }

  // read either int<N> or int<2N> into a uint<2N>_t, size is the int size in
  // bytes.
  bool getUInt16Var(off64_t offset, uint16_t* x, size_t size) {
    if (size == 2) {
      return getUInt16(offset, x);
    }
    if (size == 1) {
      uint8_t tmp;
      if (readAt(offset, &tmp, 1) == 1) {
        *x = tmp;
        return true;
      }
    }
    return false;
  }
  bool getUInt32Var(off64_t offset, uint32_t* x, size_t size) {
    if (size == 4) {
      return getUInt32(offset, x);
    }
    if (size == 2) {
      uint16_t tmp;
      if (getUInt16(offset, &tmp)) {
        *x = tmp;
        return true;
      }
    }
    return false;
  }
  bool getUInt64Var(off64_t offset, uint64_t* x, size_t size) {
    if (size == 8) {
      return getUInt64(offset, x);
    }
    if (size == 4) {
      uint32_t tmp;
      if (getUInt32(offset, &tmp)) {
        *x = tmp;
        return true;
      }
    }
    return false;
  }

  virtual status_t getSize(off64_t* size) {
    *size = 0;
    return -1;
  }

  virtual bool getUri(char* /*uriString*/, size_t /*bufferSize*/) {
    return false;
  }

  virtual uint32_t flags() { return 0; }

  virtual void close() {}

  virtual status_t getAvailableSize(off64_t /*offset*/, off64_t* /*size*/) {
    return -1;
  }
};

} /* namespace avp */

#endif /* !DATA_SOURCE_H */
