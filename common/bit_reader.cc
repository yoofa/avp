/*
 * bit_reader.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "bit_reader.h"

#include "base/checks.h"

namespace avp {

BitReader::BitReader(const uint8_t* data, size_t size)
    : mData(data),
      mSize(size),
      mReservoir(0),
      mNumBitsLeft(0),
      mOverRead(false) {}

BitReader::~BitReader() {}

bool BitReader::fillReservoir() {
  if (mSize == 0) {
    mOverRead = true;
    return false;
  }

  mReservoir = 0;
  size_t i;
  for (i = 0; mSize > 0 && i < 4; ++i) {
    mReservoir = (mReservoir << 8) | *mData;

    ++mData;
    --mSize;
  }

  mNumBitsLeft = 8 * i;
  mReservoir <<= 32 - mNumBitsLeft;
  return true;
}

uint32_t BitReader::getBits(size_t n) {
  uint32_t ret;
  CHECK(getBitsGraceful(n, &ret));
  return ret;
}

uint32_t BitReader::getBitsWithFallback(size_t n, uint32_t fallback) {
  uint32_t ret = fallback;
  (void)getBitsGraceful(n, &ret);
  return ret;
}

bool BitReader::getBitsGraceful(size_t n, uint32_t* out) {
  if (n > 32) {
    return false;
  }

  uint32_t result = 0;
  while (n > 0) {
    if (mNumBitsLeft == 0) {
      if (!fillReservoir()) {
        return false;
      }
    }

    size_t m = n;
    if (m > mNumBitsLeft) {
      m = mNumBitsLeft;
    }

    result = (result << m) | (mReservoir >> (32 - m));
    mReservoir <<= m;
    mNumBitsLeft -= m;

    n -= m;
  }

  *out = result;
  return true;
}

bool BitReader::skipBits(size_t n) {
  uint32_t dummy;
  while (n > 32) {
    if (!getBitsGraceful(32, &dummy)) {
      return false;
    }
    n -= 32;
  }

  if (n > 0) {
    return getBitsGraceful(n, &dummy);
  }
  return true;
}

void BitReader::putBits(uint32_t x, size_t n) {
  if (mOverRead) {
    return;
  }

  CHECK_LE(n, 32u);

  while (mNumBitsLeft + n > 32) {
    mNumBitsLeft -= 8;
    --mData;
    ++mSize;
  }

  mReservoir = (mReservoir >> n) | (x << (32 - n));
  mNumBitsLeft += n;
}

size_t BitReader::numBitsLeft() const {
  return mSize * 8 + mNumBitsLeft;
}

const uint8_t* BitReader::data() const {
  return mData - (mNumBitsLeft + 7) / 8;
}

NALBitReader::NALBitReader(const uint8_t* data, size_t size)
    : BitReader(data, size), mNumZeros(0) {}

bool NALBitReader::atLeastNumBitsLeft(size_t n) const {
  // check against raw size and reservoir bits first
  size_t numBits = numBitsLeft();
  if (n > numBits) {
    return false;
  }

  ssize_t numBitsRemaining = (ssize_t)n - (ssize_t)mNumBitsLeft;

  size_t size = mSize;
  const uint8_t* data = mData;
  int32_t numZeros = mNumZeros;
  while (size > 0 && numBitsRemaining > 0) {
    bool isEmulationPreventionByte = (numZeros >= 2 && *data == 3);

    if (*data == 0) {
      ++numZeros;
    } else {
      numZeros = 0;
    }

    if (!isEmulationPreventionByte) {
      numBitsRemaining -= 8;
    }

    ++data;
    --size;
  }

  return (numBitsRemaining <= 0);
}

bool NALBitReader::fillReservoir() {
  if (mSize == 0) {
    mOverRead = true;
    return false;
  }

  mReservoir = 0;
  size_t i = 0;
  while (mSize > 0 && i < 4) {
    bool isEmulationPreventionByte = (mNumZeros >= 2 && *mData == 3);

    if (*mData == 0) {
      ++mNumZeros;
    } else {
      mNumZeros = 0;
    }

    // skip emulation_prevention_three_byte
    if (!isEmulationPreventionByte) {
      mReservoir = (mReservoir << 8) | *mData;
      ++i;
    }

    ++mData;
    --mSize;
  }

  mNumBitsLeft = 8 * i;
  mReservoir <<= 32 - mNumBitsLeft;
  return true;
}
} /* namespace avp */
