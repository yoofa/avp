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

namespace ave {
namespace player {

enum SeekMode : int32_t {
  SEEK_PREVIOUS_SYNC = 0,
  SEEK_NEXT_SYNC = 1,
  SEEK_CLOSEST_SYNC = 2,
  SEEK_CLOSEST = 4,
  SEEK_FRAME_INDEX = 8,
  SEEK = 8,
  NONBLOCKING = 16,
};

}  // namespace player
}  // namespace ave

#endif /* !PLAYER_INTERFACE_H */
