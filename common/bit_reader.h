/*
 * bit_reader.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef BIT_READER_H
#define BIT_READER_H

#include "base/constructor_magic.h"
#include "base/types.h"

namespace avp {

class BitReader {
 public:
  BitReader(const uint8_t* data, size_t size);
  virtual ~BitReader();

  // Tries to get |n| bits. If not successful, returns |fallback|. Otherwise,
  // returns result. Reading 0 bits will always succeed and return 0.
  uint32_t getBitsWithFallback(size_t n, uint32_t fallback);

  // Tries to get |n| bits. If not successful, returns false. Otherwise, stores
  // result in |out| and returns true. Use !overRead() to determine if this call
  // was successful. Reading 0 bits will always succeed and write 0 in |out|.
  bool getBitsGraceful(size_t n, uint32_t* out);

  // Gets |n| bits and returns result. ABORTS if unsuccessful. Reading 0 bits
  // will always succeed.
  uint32_t getBits(size_t n);

  // Tries to skip |n| bits. Returns true iff successful. Skipping 0 bits will
  // always succeed.
  bool skipBits(size_t n);

  // "Puts" |n| bits with the value |x| back virtually into the bit stream. The
  // put-back bits are not actually written into the data, but are tracked in a
  // separate buffer that can store at most 32 bits. This is a no-op if the
  // stream has already been over-read.
  void putBits(uint32_t x, size_t n);

  size_t numBitsLeft() const;

  const uint8_t* data() const;

  // Returns true iff the stream was over-read (e.g. any getBits operation has
  // been unsuccessful due to overread (and not trying to read >32 bits).)
  bool overRead() const { return mOverRead; }

 protected:
  const uint8_t* mData;
  size_t mSize;

  uint32_t mReservoir;  // left-aligned bits
  size_t mNumBitsLeft;
  bool mOverRead;

  virtual bool fillReservoir();

  AVP_DISALLOW_COPY_AND_ASSIGN(BitReader);
};

class NALBitReader : public BitReader {
 public:
  NALBitReader(const uint8_t* data, size_t size);

  bool atLeastNumBitsLeft(size_t n) const;

 private:
  int32_t mNumZeros;

  virtual bool fillReservoir();

  AVP_DISALLOW_COPY_AND_ASSIGN(NALBitReader);
};
} /* namespace avp */

#endif /* !BIT_READER_H */
