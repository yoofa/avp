/*
 * errors.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "errors.h"

#include <string>

#include <string.h>

namespace avp {

std::string statusToString(status_t s) {
#define STATUS_CASE(STATUS) \
  case STATUS:              \
    return #STATUS

  switch (s) {
    STATUS_CASE(OK);
    STATUS_CASE(UNKNOWN_ERROR);
    STATUS_CASE(NO_MEMORY);
    STATUS_CASE(INVALID_OPERATION);
    STATUS_CASE(BAD_VALUE);
    STATUS_CASE(BAD_TYPE);
    STATUS_CASE(NAME_NOT_FOUND);
    STATUS_CASE(PERMISSION_DENIED);
    STATUS_CASE(NO_INIT);
    STATUS_CASE(ALREADY_EXISTS);
    STATUS_CASE(DEAD_OBJECT);
    STATUS_CASE(FAILED_TRANSACTION);
    STATUS_CASE(BAD_INDEX);
    STATUS_CASE(NOT_ENOUGH_DATA);
    STATUS_CASE(WOULD_BLOCK);
    STATUS_CASE(TIMED_OUT);
    STATUS_CASE(UNKNOWN_TRANSACTION);
    STATUS_CASE(FDS_NOT_ALLOWED);
    STATUS_CASE(UNEXPECTED_NULL);
#undef STATUS_CASE
  }

  return std::to_string(s) + " (" + strerror(-s) + ")";
}
} /* namespace avp */
