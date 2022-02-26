/*
 * byte_utils.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "byte_utils.h"

namespace avp {

uint16_t U16_AT(const uint8_t* ptr) {
  return ptr[0] << 8 | ptr[1];
}

uint32_t U32_AT(const uint8_t* ptr) {
  return ptr[0] << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
}

uint64_t U64_AT(const uint8_t* ptr) {
  return ((uint64_t)U32_AT(ptr)) << 32 | U32_AT(ptr + 4);
}

uint16_t U16LE_AT(const uint8_t* ptr) {
  return ptr[0] | (ptr[1] << 8);
}

uint32_t U32LE_AT(const uint8_t* ptr) {
  return ptr[3] << 24 | ptr[2] << 16 | ptr[1] << 8 | ptr[0];
}

uint64_t U64LE_AT(const uint8_t* ptr) {
  return ((uint64_t)U32LE_AT(ptr + 4)) << 32 | U32LE_AT(ptr);
}

// XXX warning: these won't work on big-endian host.
uint64_t ntoh64(uint64_t x) {
  return ((uint64_t)ntohl(x & 0xffffffff) << 32) | ntohl(x >> 32);
}

uint64_t hton64(uint64_t x) {
  return ((uint64_t)htonl(x & 0xffffffff) << 32) | htonl(x >> 32);
}

void MakeFourCCString(uint32_t x, char* s) {
  s[0] = x >> 24;
  s[1] = (x >> 16) & 0xff;
  s[2] = (x >> 8) & 0xff;
  s[3] = x & 0xff;
  s[4] = '\0';
}
} /* namespace avp */
