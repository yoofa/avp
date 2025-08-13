/*
 * avsync_controller.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "player/avsync_controller.h"

#include <algorithm>

#include "base/time_utils.h"

namespace ave {
namespace player {

AVSyncControllerImpl::AVSyncControllerImpl()
    : anchor_media_pts_us_(0),
      anchor_sys_time_us_(0),
      max_media_time_us_(-1),
      paused_(false),
      pause_sys_time_us_(0),
      pause_media_pts_us_(0),
      playback_rate_(1.0f),
      clock_type_(ClockType::kAudio) {}

AVSyncControllerImpl::~AVSyncControllerImpl() = default;

int64_t AVSyncControllerImpl::GetCurrentSystemTimeUs() const {
  return base::TimeMicros();
}

void AVSyncControllerImpl::UpdateAnchor(int64_t media_pts_us,
                                        int64_t sys_time_us,
                                        int64_t max_media_time_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  anchor_media_pts_us_ = media_pts_us;
  anchor_sys_time_us_ = sys_time_us;
  max_media_time_us_ =
      std::max({media_pts_us, max_media_time_us, max_media_time_us_});
  if (paused_) {
    // If paused, update pause anchor as well
    pause_media_pts_us_ = media_pts_us;
    pause_sys_time_us_ = sys_time_us;
  }
}

int64_t AVSyncControllerImpl::GetMasterClock() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (max_media_time_us_ == -1) {
    return 0;
  }

  if (clock_type_ == ClockType::kSystem) {
    // For system clock, return elapsed time since start
    const int64_t now_us = GetCurrentSystemTimeUs();
    int64_t delta = now_us - anchor_sys_time_us_;
    if (delta < 0) {
      delta = 0;
    }
    return anchor_media_pts_us_ +
           static_cast<int64_t>(static_cast<float>(delta) * playback_rate_);
  }

  // For audio clock (default)
  if (paused_) {
    return pause_media_pts_us_;
  }

  // Get current system time in microseconds
  const int64_t now_us = GetCurrentSystemTimeUs();
  int64_t delta = now_us - anchor_sys_time_us_;
  if (delta < 0) {
    delta = 0;
  }

  const int64_t current_media_time =
      anchor_media_pts_us_ +
      static_cast<int64_t>(static_cast<float>(delta) * playback_rate_);

  return std::min(current_media_time, max_media_time_us_);
}

void AVSyncControllerImpl::SetPlaybackRate(float rate) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (rate < 0.0f) {
    rate = 0.0f;  // Clamp to non-negative
  }
  playback_rate_ = rate;
}

float AVSyncControllerImpl::GetPlaybackRate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return playback_rate_;
}

void AVSyncControllerImpl::SetClockType(ClockType type) {
  std::lock_guard<std::mutex> lock(mutex_);
  clock_type_ = type;
}

IAVSyncController::ClockType AVSyncControllerImpl::GetClockType() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return clock_type_;
}

void AVSyncControllerImpl::Pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!paused_) {
    // Record the pause anchor
    pause_sys_time_us_ = GetCurrentSystemTimeUs();
    int64_t delta = pause_sys_time_us_ - anchor_sys_time_us_;
    if (delta < 0) {
      delta = 0;
    }
    pause_media_pts_us_ =
        anchor_media_pts_us_ +
        static_cast<int64_t>(static_cast<float>(delta) * playback_rate_);
    paused_ = true;
  }
}

void AVSyncControllerImpl::Resume() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (paused_) {
    const int64_t now_us = GetCurrentSystemTimeUs();
    // Adjust anchor to account for pause duration
    anchor_sys_time_us_ = now_us;
    anchor_media_pts_us_ = pause_media_pts_us_;
    paused_ = false;
  }
}

void AVSyncControllerImpl::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  anchor_media_pts_us_ = 0;
  anchor_sys_time_us_ = 0;
  max_media_time_us_ = -1;
  paused_ = false;
  pause_sys_time_us_ = 0;
  pause_media_pts_us_ = 0;
  playback_rate_ = 1.0f;
  clock_type_ = ClockType::kAudio;
}

}  // namespace player
}  // namespace ave
