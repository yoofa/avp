/*
 * player_interface.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PLAYER_INTERFACE_H
#define PLAYER_INTERFACE_H

#include <cstdint>
#include "base/errors.h"

namespace avp {

using status_t = ave::status_t;

enum media_track_type {
  MEDIA_TRACK_TYPE_UNKNOWN = 0,
  MEDIA_TRACK_TYPE_VIDEO = 1,
  MEDIA_TRACK_TYPE_AUDIO = 2,
  MEDIA_TRACK_TYPE_TIMEDTEXT = 3,
  MEDIA_TRACK_TYPE_SUBTITLE = 4,
  MEDIA_TRACK_TYPE_METADATA = 5,
};

enum SeekMode : int32_t {
  SEEK_PREVIOUS_SYNC = 0,
  SEEK_NEXT_SYNC = 1,
  SEEK_CLOSEST_SYNC = 2,
  SEEK_CLOSEST = 4,
  SEEK_FRAME_INDEX = 8,
  SEEK = 8,
  NONBLOCKING = 16,
};
}  // namespace avp

#endif /* !PLAYER_INTERFACE_H */
