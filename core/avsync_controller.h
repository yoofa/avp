/*
 * avsync_controller.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#ifndef AVP_PLAYER_AVSYNC_CONTROLLER_H_
#define AVP_PLAYER_AVSYNC_CONTROLLER_H_

#include <cstdint>
#include <mutex>
#include "api/player_interface.h"

namespace ave {
namespace player {

/**
 * @brief Implementation of IAVSyncController. Maintains the master media clock.
 */
class AVSyncControllerImpl : public IAVSyncController {
 public:
  AVSyncControllerImpl();
  ~AVSyncControllerImpl() override;

  /**
   * @copydoc IAVSyncController::UpdateAnchor
   */
  void UpdateAnchor(int64_t media_pts_us,
                    int64_t sys_time_us,
                    int64_t max_media_time_us) override;

  /**
   * @copydoc IAVSyncController::GetMasterClock
   */
  int64_t GetMasterClock() const override;

  /**
   * @copydoc IAVSyncController::SetPlaybackRate
   */
  void SetPlaybackRate(float rate) override;

  /**
   * @copydoc IAVSyncController::GetPlaybackRate
   */
  float GetPlaybackRate() const override;

  /**
   * @copydoc IAVSyncController::SetClockType
   */
  void SetClockType(ClockType type) override;

  /**
   * @copydoc IAVSyncController::GetClockType
   */
  ClockType GetClockType() const override;

  /**
   * @copydoc IAVSyncController::Pause
   */
  void Pause() override;

  /**
   * @copydoc IAVSyncController::Resume
   */
  void Resume() override;

  /**
   * @copydoc IAVSyncController::Reset
   */
  void Reset() override;

 protected:
  /**
   * @brief Get current system time in microseconds.
   *        This method can be overridden for testing purposes.
   * @return Current system time in microseconds.
   */
  virtual int64_t GetCurrentSystemTimeUs() const;

 private:
  mutable std::mutex mutex_;
  int64_t anchor_media_pts_us_;
  int64_t anchor_sys_time_us_;
  int64_t max_media_time_us_;
  bool paused_;
  int64_t pause_sys_time_us_;
  int64_t pause_media_pts_us_;
  float playback_rate_;
  ClockType clock_type_;
};

}  // namespace player
}  // namespace ave

#endif  // AVP_PLAYER_AVSYNC_CONTROLLER_H_
