/*
 * avsync_controller_unittest.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "player/avsync_controller.h"

#include <gtest/gtest.h>
#include <thread>

namespace ave {
namespace player {

namespace {
/**
 * @brief Mock AVSyncController for testing with controlled time progression.
 */
class MockAVSyncController : public AVSyncControllerImpl {
 public:
  MockAVSyncController() : current_time_us_(0) {}

  /**
   * @brief Set the current mock time.
   * @param time_us Time in microseconds.
   */
  void SetCurrentTime(int64_t time_us) { current_time_us_ = time_us; }

  /**
   * @brief Advance the mock time by the specified amount.
   * @param delta_us Time to advance in microseconds.
   */
  void AdvanceTime(int64_t delta_us) { current_time_us_ += delta_us; }

 protected:
  int64_t GetCurrentSystemTimeUs() const override { return current_time_us_; }

 private:
  int64_t current_time_us_;
};
}  // namespace

class AVSyncControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    controller_ = std::make_unique<MockAVSyncController>();
  }

  void TearDown() override { controller_.reset(); }

  std::unique_ptr<MockAVSyncController> controller_;
};

TEST_F(AVSyncControllerTest, InitialState) {
  EXPECT_EQ(controller_->GetMasterClock(), 0);
  EXPECT_EQ(controller_->GetPlaybackRate(), 1.0f);
  EXPECT_EQ(controller_->GetClockType(), IAVSyncController::ClockType::kAudio);
}

TEST_F(AVSyncControllerTest, UpdateAnchor) {
  const int64_t media_pts = 1000000;       // 1 second
  const int64_t sys_time = 5000000;        // 5 seconds
  const int64_t max_media_time = 2000000;  // 2 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);

  // Should return the anchor media time immediately
  EXPECT_EQ(controller_->GetMasterClock(), media_pts);
}

TEST_F(AVSyncControllerTest, UpdateAnchorWithMaxMediaTime) {
  const int64_t media_pts = 1000000;       // 1 second
  const int64_t sys_time = 5000000;        // 5 seconds
  const int64_t max_media_time = 1500000;  // 1.5 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);

  // Advance time by 2 seconds
  controller_->AdvanceTime(2000000);

  // Should be limited by max_media_time
  const int64_t current_time = controller_->GetMasterClock();
  EXPECT_LE(current_time, max_media_time);
}

TEST_F(AVSyncControllerTest, PlaybackRate) {
  controller_->SetPlaybackRate(2.0f);
  EXPECT_EQ(controller_->GetPlaybackRate(), 2.0f);

  controller_->SetPlaybackRate(0.5f);
  EXPECT_EQ(controller_->GetPlaybackRate(), 0.5f);

  // Test negative rate clamping
  controller_->SetPlaybackRate(-1.0f);
  EXPECT_EQ(controller_->GetPlaybackRate(), 0.0f);
}

TEST_F(AVSyncControllerTest, ClockTypeSwitching) {
  // Test initial state
  EXPECT_EQ(controller_->GetClockType(), IAVSyncController::ClockType::kAudio);

  // Switch to system clock
  controller_->SetClockType(IAVSyncController::ClockType::kSystem);
  EXPECT_EQ(controller_->GetClockType(), IAVSyncController::ClockType::kSystem);

  // Switch back to audio clock
  controller_->SetClockType(IAVSyncController::ClockType::kAudio);
  EXPECT_EQ(controller_->GetClockType(), IAVSyncController::ClockType::kAudio);
}

TEST_F(AVSyncControllerTest, PauseAndResume) {
  const int64_t media_pts = 1000000;       // 1 second
  const int64_t sys_time = 5000000;        // 5 seconds
  const int64_t max_media_time = 2000000;  // 2 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);
  controller_->SetCurrentTime(sys_time);

  // Advance time by 100ms
  controller_->AdvanceTime(100000);

  // Pause
  controller_->Pause();
  const int64_t pause_time = controller_->GetMasterClock();

  // Advance time by 200ms - time should not advance
  controller_->AdvanceTime(200000);
  EXPECT_EQ(controller_->GetMasterClock(), pause_time);

  // Resume
  controller_->Resume();
  const int64_t resume_time = controller_->GetMasterClock();
  EXPECT_EQ(resume_time, pause_time);

  // Advance time by 100ms - time should advance again
  controller_->AdvanceTime(100000);
  EXPECT_GT(controller_->GetMasterClock(), resume_time);
}

TEST_F(AVSyncControllerTest, Reset) {
  // Set some state
  controller_->SetPlaybackRate(2.0f);
  controller_->SetClockType(IAVSyncController::ClockType::kSystem);
  controller_->UpdateAnchor(1000000, 5000000, 2000000);
  controller_->Pause();

  // Reset
  controller_->Reset();

  // Should be back to initial state
  EXPECT_EQ(controller_->GetMasterClock(), 0);
  EXPECT_EQ(controller_->GetPlaybackRate(), 1.0f);
  EXPECT_EQ(controller_->GetClockType(), IAVSyncController::ClockType::kAudio);
}

TEST_F(AVSyncControllerTest, SystemClockMode) {
  controller_->SetClockType(IAVSyncController::ClockType::kSystem);

  const int64_t media_pts = 1000000;       // 1 second
  const int64_t sys_time = 5000000;        // 5 seconds
  const int64_t max_media_time = 2000000;  // 2 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);
  controller_->SetCurrentTime(sys_time);

  // Advance time by 500ms
  controller_->AdvanceTime(500000);

  // Should advance by exactly 500ms
  const int64_t current_time = controller_->GetMasterClock();
  EXPECT_EQ(current_time, media_pts + 500000);
}

TEST_F(AVSyncControllerTest, AudioClockMode) {
  controller_->SetClockType(IAVSyncController::ClockType::kAudio);

  const int64_t media_pts = 1000000;       // 1 second
  const int64_t sys_time = 5000000;        // 5 seconds
  const int64_t max_media_time = 2000000;  // 2 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);
  controller_->SetCurrentTime(sys_time);

  // Advance time by 500ms
  controller_->AdvanceTime(500000);

  // Should advance by exactly 500ms
  const int64_t current_time = controller_->GetMasterClock();
  EXPECT_EQ(current_time, media_pts + 500000);
}

TEST_F(AVSyncControllerTest, PlaybackRateWithSystemClock) {
  controller_->SetClockType(IAVSyncController::ClockType::kSystem);
  controller_->SetPlaybackRate(2.0f);

  const int64_t media_pts = 1000000;       // 1 second
  const int64_t sys_time = 5000000;        // 5 seconds
  const int64_t max_media_time = 2000000;  // 2 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);
  controller_->SetCurrentTime(sys_time);

  // Advance time by 500ms
  controller_->AdvanceTime(500000);

  // Should advance by exactly 1000ms (500ms * 2.0 rate)
  const int64_t current_time = controller_->GetMasterClock();
  EXPECT_EQ(current_time, media_pts + 1000000);
}

TEST_F(AVSyncControllerTest, PlaybackRateWithAudioClock) {
  controller_->SetClockType(IAVSyncController::ClockType::kAudio);
  controller_->SetPlaybackRate(0.5f);

  const int64_t media_pts = 1000000;       // 1 second
  const int64_t sys_time = 5000000;        // 5 seconds
  const int64_t max_media_time = 2000000;  // 2 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);
  controller_->SetCurrentTime(sys_time);

  // Advance time by 1000ms
  controller_->AdvanceTime(1000000);

  // Should advance by exactly 500ms (1000ms * 0.5 rate)
  const int64_t current_time = controller_->GetMasterClock();
  EXPECT_EQ(current_time, media_pts + 500000);
}

TEST_F(AVSyncControllerTest, MultiplePauseResume) {
  const int64_t media_pts = 1000000;
  const int64_t sys_time = 5000000;
  const int64_t max_media_time = 2000000;  // 2 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);
  controller_->SetCurrentTime(sys_time);

  // First pause/resume cycle
  controller_->AdvanceTime(100000);
  controller_->Pause();
  const int64_t pause1 = controller_->GetMasterClock();

  controller_->AdvanceTime(100000);
  controller_->Resume();

  // Second pause/resume cycle
  controller_->AdvanceTime(100000);
  controller_->Pause();
  const int64_t pause2 = controller_->GetMasterClock();

  controller_->AdvanceTime(100000);
  controller_->Resume();

  // Third pause/resume cycle
  controller_->AdvanceTime(100000);
  controller_->Pause();
  const int64_t pause3 = controller_->GetMasterClock();

  // Verify that time advanced between pauses
  EXPECT_GT(pause2, pause1);
  EXPECT_GT(pause3, pause2);
}

TEST_F(AVSyncControllerTest, UpdateAnchorDuringPause) {
  const int64_t media_pts1 = 1000000;
  const int64_t sys_time1 = 5000000;
  const int64_t max_media_time = 4000000;  // 4 seconds

  controller_->UpdateAnchor(media_pts1, sys_time1, max_media_time);
  controller_->SetCurrentTime(sys_time1);

  controller_->AdvanceTime(100000);
  controller_->Pause();

  // Update anchor while paused
  const int64_t media_pts2 = 2000000;
  const int64_t sys_time2 = 5100000;
  controller_->SetCurrentTime(sys_time2);
  controller_->UpdateAnchor(media_pts2, sys_time2, max_media_time);

  // Should reflect the new anchor
  EXPECT_EQ(controller_->GetMasterClock(), media_pts2);

  controller_->Resume();

  // Should continue from the new anchor
  controller_->AdvanceTime(100000);
  const int64_t current_time = controller_->GetMasterClock();
  EXPECT_GT(current_time, media_pts2);
}

TEST_F(AVSyncControllerTest, MaxMediaTimeLimit) {
  const int64_t media_pts = 1000000;
  const int64_t sys_time = 5000000;
  const int64_t max_media_time = 1500000;  // 1.5 seconds

  controller_->UpdateAnchor(media_pts, sys_time, max_media_time);
  controller_->SetCurrentTime(sys_time);

  // Advance time by 2 seconds
  controller_->AdvanceTime(2000000);

  // Should be capped at max_media_time
  int64_t current_time = controller_->GetMasterClock();
  EXPECT_EQ(current_time, max_media_time);

  // Update anchor with new max_media_time
  const int64_t new_max_media_time = 3000000;  // 3 seconds
  controller_->UpdateAnchor(media_pts, sys_time, new_max_media_time);

  // Should now be able to advance beyond the old limit
  controller_->AdvanceTime(1000000);
  current_time = controller_->GetMasterClock();
  EXPECT_GT(current_time, max_media_time);
  EXPECT_LE(current_time, new_max_media_time);
}

TEST_F(AVSyncControllerTest, ThreadSafety) {
  const int kNumThreads = 4;
  const int kNumOperations = 1000;

  std::vector<std::thread> threads;

  // Start multiple threads that perform different operations
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([this, i]() {
      for (int j = 0; j < kNumOperations; ++j) {
        switch (i % 4) {
          case 0:
            controller_->UpdateAnchor(j * 1000, j * 1000, j * 1000 + 500000);
            break;
          case 1:
            controller_->GetMasterClock();
            break;
          case 2:
            controller_->SetPlaybackRate(0.5f +
                                         static_cast<float>(j % 10) * 0.1f);
            break;
          case 3:
            controller_->SetClockType(
                j % 2 == 0 ? IAVSyncController::ClockType::kSystem
                           : IAVSyncController::ClockType::kAudio);
            break;
        }
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Should not crash and should be in a valid state
  EXPECT_GE(controller_->GetMasterClock(), 0);
  EXPECT_GE(controller_->GetPlaybackRate(), 0.0f);
}

}  // namespace player
}  // namespace ave
