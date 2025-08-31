/*
 * avp_render_unittest.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_render.h"

#include <memory>
#include <thread>

#include "base/logging.h"
#include "media/foundation/media_frame.h"

#include "api/player_interface.h"
#include "mock_task_runner_factory.h"

#include "test/gtest.h"

namespace ave {
namespace player {

namespace {
// Mock AVSyncController for testing
class MockAVSyncController : public IAVSyncController {
 public:
  MockAVSyncController() : current_time_us_(0), playback_rate_(1.0f) {}

  ~MockAVSyncController() override = default;

  void UpdateAnchor(int64_t media_pts_us,
                    int64_t sys_time_us,
                    int64_t max_media_time_us) override {
    anchor_media_pts_us_ = media_pts_us;
    anchor_sys_time_us_ = sys_time_us;
    max_media_time_us_ = max_media_time_us;
    current_time_us_ = media_pts_us;  // Update current time when anchor is set
  }

  int64_t GetMasterClock() const override { return current_time_us_; }

  void SetPlaybackRate(float rate) override { playback_rate_ = rate; }

  float GetPlaybackRate() const override { return playback_rate_; }

  void Pause() override { paused_ = true; }

  void Resume() override { paused_ = false; }

  void Reset() override {
    anchor_media_pts_us_ = -1;
    anchor_sys_time_us_ = 0;
    max_media_time_us_ = -1;
    current_time_us_ = 0;
    paused_ = false;
  }

  void SetClockType(ClockType type) override { clock_type_ = type; }

  ClockType GetClockType() const override { return clock_type_; }

  // Test helper methods
  void SetCurrentTime(int64_t time_us) { current_time_us_ = time_us; }

  int64_t GetAnchorMediaPts() const { return anchor_media_pts_us_; }

  int64_t GetAnchorSysTime() const { return anchor_sys_time_us_; }

  int64_t GetMaxMediaTime() const { return max_media_time_us_; }

  bool IsPaused() const { return paused_; }

 private:
  int64_t anchor_media_pts_us_ = -1;
  int64_t anchor_sys_time_us_ = 0;
  int64_t max_media_time_us_ = -1;
  int64_t current_time_us_ = 0;
  float playback_rate_ = 1.0f;
  bool paused_ = false;
  ClockType clock_type_ = ClockType::kSystem;
};

// Test implementation of AVPRender
class TestAVPRender : public AVPRender {
 public:
  TestAVPRender(base::TaskRunnerFactory* task_runner_factory,
                IAVSyncController* avsync_controller)
      : AVPRender(task_runner_factory, avsync_controller) {}

  int GetRenderCount() const { return render_count_; }
  std::shared_ptr<media::MediaFrame> GetLastFrame() const {
    return last_frame_;
  }
  void ResetRenderCount() { render_count_ = 0; }

  // Expose protected state for testing
  using AVPRender::IsPaused;
  using AVPRender::IsRunning;

 protected:
  uint64_t RenderFrameInternal(std::shared_ptr<media::MediaFrame>& frame,
                               bool& consumed) override {
    render_count_++;
    last_frame_ = frame;
    consumed = true;
    return 0;
  }

 private:
  int render_count_ = 0;
  std::shared_ptr<media::MediaFrame> last_frame_ = nullptr;
};
}  // namespace

class AVPRenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_task_runner_factory_ =
        std::make_unique<player::MockTaskRunnerFactory>();
    avsync_controller_ = std::make_unique<MockAVSyncController>();
    renderer_ = std::make_unique<TestAVPRender>(mock_task_runner_factory_.get(),
                                                avsync_controller_.get());
  }

  void TearDown() override {
    renderer_->Stop();
    renderer_.reset();
    avsync_controller_.reset();
    mock_task_runner_factory_.reset();
  }

  // 只推进调度时间
  void AdvanceTaskRunnerTimeUs(uint64_t delta_us) {
    mock_task_runner_factory_->runner()->AdvanceTimeUs(delta_us);
  }

  // 只推进媒体时钟
  void AdvanceMediaClockUs(uint64_t delta_us) {
    int64_t current_clock = avsync_controller_->GetMasterClock();
    int64_t new_time = current_clock + delta_us;
    avsync_controller_->SetCurrentTime(new_time);
  }
  // 同时推进两者
  void AdvanceBothUs(uint64_t delta_us) {
    AdvanceMediaClockUs(delta_us);
    AdvanceTaskRunnerTimeUs(delta_us);
  }

  // 触发任务调度
  void TriggerTaskRunner() {
    mock_task_runner_factory_->runner()->RunDueTasks();
  }

  std::shared_ptr<media::MediaFrame> CreateTestFrame(media::MediaType type,
                                                     int64_t pts_us) {
    auto frame = media::MediaFrame::Create(1024);
    frame.SetMediaType(type);

    if (type == media::MediaType::AUDIO) {
      auto* audio_info = frame.audio_info();
      if (audio_info) {
        audio_info->pts = base::Timestamp::Micros(pts_us);
      }
    } else if (type == media::MediaType::VIDEO) {
      auto* video_info = frame.video_info();
      if (video_info) {
        video_info->pts = base::Timestamp::Micros(pts_us);
      }
    }

    return std::make_shared<media::MediaFrame>(frame);
  }

  // Helper method to flush all scheduled tasks in the mock runner
  void FlushTaskRunner() {
    mock_task_runner_factory_->runner()->ClearAllTasks();
  }

  std::unique_ptr<player::MockTaskRunnerFactory> mock_task_runner_factory_;
  std::unique_ptr<MockAVSyncController> avsync_controller_;
  std::unique_ptr<TestAVPRender> renderer_;
};

TEST_F(AVPRenderTest, InitialState) {
  EXPECT_FALSE(renderer_->IsRunning());
  EXPECT_FALSE(renderer_->IsPaused());
  EXPECT_EQ(renderer_->GetCurrentTimeStamp(), 0);

  avsync_controller_->SetClockType(IAVSyncController::ClockType::kSystem);
  EXPECT_EQ(avsync_controller_->GetClockType(),
            IAVSyncController::ClockType::kSystem);

  // Test avsync_controller_ directly
  MockAVSyncController* mock_ptr =
      dynamic_cast<MockAVSyncController*>(avsync_controller_.get());
  EXPECT_NE(mock_ptr, nullptr);

  // Test direct method calls
  int64_t clock1 = mock_ptr->GetMasterClock();
  EXPECT_EQ(clock1, 0);

  mock_ptr->SetCurrentTime(500000);

  int64_t clock2 = mock_ptr->GetMasterClock();
  EXPECT_EQ(clock2, 500000);

  // Test virtual function calls
  int64_t clock3 = avsync_controller_->GetMasterClock();
  EXPECT_EQ(clock3, 500000);

  avsync_controller_->SetCurrentTime(1000000);

  int64_t clock4 = avsync_controller_->GetMasterClock();
  EXPECT_EQ(clock4, 1000000);
}

TEST_F(AVPRenderTest, StartStop) {
  renderer_->Start();
  EXPECT_TRUE(renderer_->IsRunning());
  EXPECT_FALSE(renderer_->IsPaused());

  renderer_->Stop();
  EXPECT_FALSE(renderer_->IsRunning());
  EXPECT_FALSE(renderer_->IsPaused());
}

TEST_F(AVPRenderTest, PauseResume) {
  renderer_->Start();
  EXPECT_TRUE(renderer_->IsRunning());

  renderer_->Pause();
  EXPECT_TRUE(renderer_->IsRunning());
  EXPECT_TRUE(renderer_->IsPaused());

  renderer_->Resume();
  EXPECT_TRUE(renderer_->IsRunning());
  EXPECT_FALSE(renderer_->IsPaused());
}

TEST_F(AVPRenderTest, Flush) {
  renderer_->Start();
  renderer_->Flush();
  // Flush should increment generation but not change running/paused state
  EXPECT_TRUE(renderer_->IsRunning());
}

TEST_F(AVPRenderTest, RenderFrameWhenNotRunning) {
  auto frame = CreateTestFrame(media::MediaType::VIDEO, 1000000);
  renderer_->RenderFrame(frame);

  // Frame should not be rendered when not running
  EXPECT_EQ(renderer_->GetRenderCount(), 0);
}

TEST_F(AVPRenderTest, RenderFrameWhenPaused) {
  renderer_->Start();
  renderer_->Pause();

  auto frame = CreateTestFrame(media::MediaType::VIDEO, 1000000);
  renderer_->RenderFrame(frame);
  TriggerTaskRunner();

  // Frame should be queued but not rendered when paused
  EXPECT_EQ(renderer_->GetRenderCount(), 0);

  // Resume should render the queued frame
  renderer_->Resume();
  // Advance time to trigger the delayed task
  AdvanceBothUs(1000000);  // Advance 1 second
  TriggerTaskRunner();
  EXPECT_EQ(renderer_->GetRenderCount(), 1);
}

TEST_F(AVPRenderTest, RenderFrameImmediate) {
  renderer_->Start();
  avsync_controller_->SetCurrentTime(1000000);  // 1 second

  auto frame = CreateTestFrame(media::MediaType::VIDEO, 1000000);
  renderer_->RenderFrame(frame);

  // Frame should be rendered immediately since PTS matches current time
  TriggerTaskRunner();  // Execute due tasks
  EXPECT_EQ(renderer_->GetRenderCount(), 1);
  EXPECT_EQ(renderer_->GetLastFrame(), frame);
}

TEST_F(AVPRenderTest, RenderFrameDelayed) {
  renderer_->Start();
  avsync_controller_->SetCurrentTime(1000000);  // 1 second

  auto frame = CreateTestFrame(media::MediaType::VIDEO, 2000000);  // 2 seconds
  renderer_->RenderFrame(frame);

  // Frame should not be rendered immediately since it's scheduled for future
  EXPECT_EQ(renderer_->GetRenderCount(), 0);

  // 先推进调度时间，再将媒体时钟推进到frame的pts，确保delay_us为0
  avsync_controller_->SetCurrentTime(2000000);  // 直接设置为frame的pts
  AdvanceTaskRunnerTimeUs(1000000);             // Advance to 2 seconds
  TriggerTaskRunner();                          // Execute due tasks

  EXPECT_EQ(renderer_->GetRenderCount(), 1);
}

TEST_F(AVPRenderTest, RenderFrameLate) {
  renderer_->Start();
  avsync_controller_->SetCurrentTime(2000000);  // 2 seconds

  auto frame = CreateTestFrame(media::MediaType::VIDEO,
                               1961000);  // 1.96 seconds (late by 40ms)
  renderer_->RenderFrame(frame);

  // Late frame should be rendered immediately (within 40ms threshold)
  TriggerTaskRunner();  // Execute due tasks
  EXPECT_EQ(renderer_->GetRenderCount(), 1);
}

TEST_F(AVPRenderTest, AudioFrameRendering) {
  renderer_->Start();
  avsync_controller_->SetCurrentTime(1000000);

  auto frame = CreateTestFrame(media::MediaType::AUDIO, 1000000);
  renderer_->RenderFrame(frame);

  TriggerTaskRunner();  // Execute due tasks
  EXPECT_EQ(renderer_->GetRenderCount(), 1);
  EXPECT_EQ(renderer_->GetLastFrame(), frame);
}

TEST_F(AVPRenderTest, NullFrameHandling) {
  renderer_->Start();

  renderer_->RenderFrame(nullptr);

  // Null frame should be ignored
  AdvanceBothUs(100000);  // Small advance
  EXPECT_EQ(renderer_->GetRenderCount(), 0);
}

TEST_F(AVPRenderTest, PauseResumeWithQueuedFrames) {
  renderer_->Start();
  avsync_controller_->SetCurrentTime(1000000);

  // Queue multiple frames with different PTS
  auto frame1 = CreateTestFrame(media::MediaType::VIDEO, 1000000);
  auto frame2 =
      CreateTestFrame(media::MediaType::VIDEO, 1100000);  // 100ms later
  auto frame3 =
      CreateTestFrame(media::MediaType::VIDEO, 1200000);  // 200ms later

  renderer_->RenderFrame(frame1);
  renderer_->RenderFrame(frame2);
  renderer_->RenderFrame(frame3);

  // Pause before any frames are rendered
  renderer_->Pause();
  TriggerTaskRunner();  // Execute due tasks
  EXPECT_EQ(renderer_->GetRenderCount(), 0);

  // Resume should render all queued frames
  renderer_->Resume();
  TriggerTaskRunner();  // Execute due tasks
  EXPECT_EQ(renderer_->GetRenderCount(), 1);
}

TEST_F(AVPRenderTest, FlushClearsQueue) {
  renderer_->Start();
  avsync_controller_->SetCurrentTime(1000000);

  // Queue frames
  auto frame1 = CreateTestFrame(media::MediaType::VIDEO, 1000000);
  auto frame2 = CreateTestFrame(media::MediaType::VIDEO, 2000000);

  renderer_->RenderFrame(frame1);
  renderer_->RenderFrame(frame2);

  // Flush should clear the queue
  renderer_->Flush();
  FlushTaskRunner();
  AdvanceBothUs(3000000);  // Advance to trigger any remaining tasks
  TriggerTaskRunner();     // Execute due tasks
  EXPECT_EQ(renderer_->GetRenderCount(), 0);
}

TEST_F(AVPRenderTest, TimestampUpdate) {
  renderer_->Start();
  avsync_controller_->SetCurrentTime(1000000);
  EXPECT_EQ(renderer_->GetCurrentTimeStamp(), 1000000);

  avsync_controller_->SetCurrentTime(2000000);
  EXPECT_EQ(renderer_->GetCurrentTimeStamp(), 2000000);
}

}  // namespace player
}  // namespace ave
