/*
 * player_interface.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PLAYER_INTERFACE_H
#define PLAYER_INTERFACE_H

#include <cstdint>

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

/**
 * @brief Interface for AV Sync Controller (master clock).
 *        Maintains the main media clock, updated by audio renderer, and
 * provides clock queries for video sync.
 */
class IAVSyncController {
 public:
  /**
   * @brief Clock type enumeration.
   */
  enum class ClockType {
    kSystem,  ///< System clock based
    kAudio,   ///< Audio clock based (default)
  };

  virtual ~IAVSyncController() = default;

  /**
   * @brief Update the clock anchor with the latest PTS and system time.
   * @param media_pts_us The latest media PTS in microseconds.
   * @param sys_time_us The corresponding system time in microseconds.
   * @param max_media_time_us The maximum media time to allow playback to
   * continue.
   */
  virtual void UpdateAnchor(int64_t media_pts_us,
                            int64_t sys_time_us,
                            int64_t max_media_time_us) = 0;

  /**
   * @brief Get the current master clock time (media time in microseconds).
   * @return The current master clock time in microseconds.
   */
  virtual int64_t GetMasterClock() const = 0;

  /**
   * @brief Set the playback rate.
   * @param rate The playback rate (1.0 = normal speed).
   */
  virtual void SetPlaybackRate(float rate) = 0;

  /**
   * @brief Get the current playback rate.
   * @return The current playback rate.
   */
  virtual float GetPlaybackRate() const = 0;

  /**
   * @brief Set the clock type.
   * @param type The clock type to use.
   */
  virtual void SetClockType(ClockType type) = 0;

  /**
   * @brief Get the current clock type.
   * @return The current clock type.
   */
  virtual ClockType GetClockType() const = 0;

  /**
   * @brief Pause the clock (e.g., on playback pause).
   */
  virtual void Pause() = 0;

  /**
   * @brief Resume the clock (e.g., on playback resume).
   */
  virtual void Resume() = 0;

  /**
   * @brief Reset the clock (e.g., on seek or stop).
   */
  virtual void Reset() = 0;
};

}  // namespace player
}  // namespace ave

#endif /* !PLAYER_INTERFACE_H */
