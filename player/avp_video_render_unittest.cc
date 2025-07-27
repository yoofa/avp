/*
 * avp_video_render_unittest.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_video_render.h"

#include <gtest/gtest.h>
#include <memory>
#include <thread>

#include "api/player_interface.h"
#include "base/test/mock_task_runner_factory.h"
#include "media/foundation/media_format.h"
#include "media/foundation/media_frame.h"
#include "media/video/video_render.h"
#include "test/gtest.h"

namespace ave {
namespace player {

// Mock AVSyncController for testing
class MockAVSyncController : public IAVSyncController {
 public:
  MockAVSyncController()
      : current_time_us_(0), playback_rate_(1.0f), paused_(false) {}

  void UpdateAnchor(int64_t media_pts_us,
                    int64_t sys_time_us,
                    int64_t max_media_time_us) override {
    anchor_media_pts_us_ = media_pts_us;
    anchor_sys_time_us_ = sys_time_us;
    max_media_time_us_ = max_media_time_us;
    update_count_++;
  }

  int64_t GetMasterClock() const override { return current_time_us_; }
  void SetPlaybackRate(float rate) override { playback_rate_ = rate; }
  float GetPlaybackRate() const override { return playback_rate_; }
  void SetClockType(ClockType type) override { clock_type_ = type; }
  ClockType GetClockType() const override { return clock_type_; }
  void Pause() override { paused_ = true; }
  void Resume() override { paused_ = false; }
  void Reset() override { current_time_us_ = 0; }

  // Test helper methods
  void SetCurrentTime(int64_t time_us) { current_time_us_ = time_us; }
  int64_t GetAnchorMediaPts() const { return anchor_media_pts_us_; }
  int64_t GetAnchorSysTime() const { return anchor_sys_time_us_; }
  int64_t GetMaxMediaTime() const { return max_media_time_us_; }
  int GetUpdateCount() const { return update_count_; }

 private:
  int64_t current_time_us_;
  float playback_rate_;
  bool paused_;
  ClockType clock_type_ = ClockType::kAudio;
  int64_t anchor_media_pts_us_ = 0;
  int64_t anchor_sys_time_us_ = 0;
  int64_t max_media_time_us_ = 0;
  int update_count_ = 0;
};

// Mock VideoRender for testing
class MockVideoRender : public media::VideoRender {
 public:
  MockVideoRender() = default;
  ~MockVideoRender() override = default;

  void OnFrame(const std::shared_ptr<media::MediaFrame>& frame) override {
    received_frames_.push_back(frame);
    frame_count_++;
  }

  int64_t render_latency() override { return render_latency_us_; }

  // Test helper methods
  void SetRenderLatency(int64_t latency_us) { render_latency_us_ = latency_us; }
  const std::vector<std::shared_ptr<media::MediaFrame>>& GetReceivedFrames()
      const {
    return received_frames_;
  }
  void ClearReceivedFrames() {
    received_frames_.clear();
    frame_count_ = 0;
  }
  int GetFrameCount() const { return frame_count_; }

 private:
  std::vector<std::shared_ptr<media::MediaFrame>> received_frames_;
  int64_t render_latency_us_ = 0;
  int frame_count_ = 0;
};

class AVPVideoRenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_task_runner_factory_ = std::make_unique<base::MockTaskRunnerFactory>();
    mock_avsync_controller_ = std::make_unique<MockAVSyncController>();
    mock_video_render_ = std::make_shared<MockVideoRender>();

    video_render_ = std::make_unique<AVPVideoRender>(
        mock_task_runner_factory_.get(), mock_avsync_controller_.get());
  }

  void TearDown() override {
    video_render_.reset();
    mock_video_render_.reset();
    mock_avsync_controller_.reset();
    mock_task_runner_factory_.reset();
  }

  // Helper method to create test video frames
  std::shared_ptr<media::MediaFrame> CreateTestVideoFrame(int64_t pts_us,
                                                          int width = 1920,
                                                          int height = 1080) {
    auto frame = std::make_shared<media::MediaFrame>(
        media::MediaFrame::Create(1024));  // Create with some data size
    frame->SetMediaType(media::MediaType::VIDEO);

    auto* video_info = frame->video_info();
    if (video_info) {
      video_info->width = width;
      video_info->height = height;
      video_info->stride = width;
      video_info->pixel_format = media::AVE_PIX_FMT_YUV420P;
      video_info->pts = base::Timestamp::Micros(pts_us);
    }

    return frame;
  }

  // Helper method to create test audio frames (for negative testing)
  std::shared_ptr<media::MediaFrame> CreateTestAudioFrame(int64_t pts_us) {
    auto frame =
        std::make_shared<media::MediaFrame>(media::MediaFrame::Create(1024));
    frame->SetMediaType(media::MediaType::AUDIO);
    return frame;
  }

  // Helper method to run pending tasks
  void RunPendingTasks() { mock_task_runner_factory_->runner()->RunDueTasks(); }

  std::unique_ptr<base::MockTaskRunnerFactory> mock_task_runner_factory_;
  std::unique_ptr<MockAVSyncController> mock_avsync_controller_;
  std::shared_ptr<MockVideoRender> mock_video_render_;
  std::unique_ptr<AVPVideoRender> video_render_;
};

// Test Suite: Basic Functionality
TEST_F(AVPVideoRenderTest, Constructor) {
  EXPECT_NE(video_render_, nullptr);
  EXPECT_EQ(video_render_->GetSink(), nullptr);
}

TEST_F(AVPVideoRenderTest, SetAndGetSink) {
  video_render_->SetSink(mock_video_render_);
  EXPECT_EQ(video_render_->GetSink(), mock_video_render_);
}

TEST_F(AVPVideoRenderTest, SetNullSink) {
  video_render_->SetSink(nullptr);
  EXPECT_EQ(video_render_->GetSink(), nullptr);
}

// Test Suite: Lifecycle Management
TEST_F(AVPVideoRenderTest, StartStop) {
  // Test that Start and Stop don't crash
  video_render_->Start();
  video_render_->Stop();
  // Success if no crash
}

TEST_F(AVPVideoRenderTest, PauseResume) {
  video_render_->Start();

  // Test that Pause and Resume don't crash
  video_render_->Pause();
  video_render_->Resume();
  video_render_->Stop();
  // Success if no crash
}

TEST_F(AVPVideoRenderTest, Flush) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  // Flush should not crash
  video_render_->Flush();
  // Success if no crash
}

// Test Suite: Frame Rendering
TEST_F(AVPVideoRenderTest, RenderFrameWithoutSink) {
  video_render_->Start();

  auto frame = CreateTestVideoFrame(1000);
  video_render_->RenderFrame(frame);
  RunPendingTasks();

  // Frame should be processed but not rendered since no sink is set
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 0U);
}

TEST_F(AVPVideoRenderTest, RenderFrameWithSink) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  auto frame = CreateTestVideoFrame(1000);
  video_render_->RenderFrame(frame);
  RunPendingTasks();

  // Frame should be passed to the video render sink
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 1U);
  EXPECT_EQ(mock_video_render_->GetReceivedFrames()[0], frame);
}

TEST_F(AVPVideoRenderTest, RenderMultipleFrames) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  auto frame1 = CreateTestVideoFrame(1000);
  auto frame2 = CreateTestVideoFrame(2000);
  auto frame3 = CreateTestVideoFrame(3000);

  int rendered = 0;
  auto render_next = [&](bool) {
    rendered++;
    RunPendingTasks();
  };

  video_render_->RenderFrame(frame1, render_next);
  video_render_->RenderFrame(frame2, render_next);
  video_render_->RenderFrame(frame3, render_next);
  RunPendingTasks();

  EXPECT_EQ(rendered, 3);
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 3U);
  EXPECT_EQ(mock_video_render_->GetReceivedFrames()[0], frame1);
  EXPECT_EQ(mock_video_render_->GetReceivedFrames()[1], frame2);
  EXPECT_EQ(mock_video_render_->GetReceivedFrames()[2], frame3);
}

TEST_F(AVPVideoRenderTest, RenderFrameWhenStopped) {
  video_render_->SetSink(mock_video_render_);
  // Don't start the renderer

  auto frame = CreateTestVideoFrame(1000);
  video_render_->RenderFrame(frame);
  RunPendingTasks();

  // Frame should not be rendered when stopped
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 0U);
}

TEST_F(AVPVideoRenderTest, RenderFrameWhenPaused) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();
  video_render_->Pause();

  auto frame = CreateTestVideoFrame(1000);
  video_render_->RenderFrame(frame);
  RunPendingTasks();

  // Frame should still be rendered when paused (pause affects timing, not
  // rendering)
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 1U);
}

// Test Suite: Error Handling
TEST_F(AVPVideoRenderTest, RenderNullFrame) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  video_render_->RenderFrame(nullptr);
  RunPendingTasks();

  // Null frame should be handled gracefully
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 0U);
}

TEST_F(AVPVideoRenderTest, RenderAudioFrame) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  auto audio_frame = CreateTestAudioFrame(1000);
  video_render_->RenderFrame(audio_frame);
  RunPendingTasks();

  // Audio frame should be rejected
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 0U);
}

// Test Suite: Render Events
TEST_F(AVPVideoRenderTest, RenderFrameWithEvent) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  bool event_called = false;
  auto frame = CreateTestVideoFrame(1000);

  video_render_->RenderFrame(frame, [&event_called](bool rendered) {
    event_called = true;
    EXPECT_TRUE(rendered);
  });
  RunPendingTasks();

  EXPECT_TRUE(event_called);
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 1U);
}

TEST_F(AVPVideoRenderTest, RenderFrameWithEventNoSink) {
  video_render_->Start();  // No sink set

  bool event_called = false;
  auto frame = CreateTestVideoFrame(1000);

  video_render_->RenderFrame(frame, [&event_called](bool rendered) {
    event_called = true;
    EXPECT_FALSE(rendered);  // Should be false when no sink
  });
  RunPendingTasks();

  EXPECT_TRUE(event_called);
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 0U);
}

// Test Suite: Format Tracking
TEST_F(AVPVideoRenderTest, FormatInitialization) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  auto frame = CreateTestVideoFrame(1000, 1920, 1080);
  video_render_->RenderFrame(frame);
  RunPendingTasks();

  // Format should be tracked internally (we can't directly test this,
  // but we can verify the frame is processed correctly)
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 1U);
}

// Test Suite: Performance and Timing
TEST_F(AVPVideoRenderTest, RenderLatency) {
  const int64_t kExpectedLatency = 5000;  // 5ms
  mock_video_render_->SetRenderLatency(kExpectedLatency);

  EXPECT_EQ(mock_video_render_->render_latency(), kExpectedLatency);
}

TEST_F(AVPVideoRenderTest, HighFrameRateRendering) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  const int kFrameCount = 100;
  std::vector<std::shared_ptr<media::MediaFrame>> frames;

  // Create many frames
  for (int i = 0; i < kFrameCount; ++i) {
    frames.push_back(CreateTestVideoFrame(i * 1000));
  }

  // Render all frames
  for (const auto& frame : frames) {
    video_render_->RenderFrame(frame);
  }
  RunPendingTasks();

  // All frames should be rendered
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(),
            static_cast<size_t>(kFrameCount));
}

// Test Suite: Thread Safety
TEST_F(AVPVideoRenderTest, ConcurrentSinkAccess) {
  video_render_->SetSink(mock_video_render_);

  // Simulate concurrent access to GetSink
  std::thread thread1([this]() {
    for (int i = 0; i < 100; ++i) {
      EXPECT_EQ(video_render_->GetSink(), mock_video_render_);
    }
  });

  std::thread thread2([this]() {
    for (int i = 0; i < 100; ++i) {
      EXPECT_EQ(video_render_->GetSink(), mock_video_render_);
    }
  });

  thread1.join();
  thread2.join();
}

// Test Suite: Integration with AVSyncController
TEST_F(AVPVideoRenderTest, AVSyncControllerIntegration) {
  video_render_->SetSink(mock_video_render_);
  video_render_->Start();

  // Set up AV sync controller
  mock_avsync_controller_->SetCurrentTime(5000);

  auto frame = CreateTestVideoFrame(1000);
  video_render_->RenderFrame(frame);
  RunPendingTasks();

  // Frame should be rendered and AV sync controller should be accessible
  EXPECT_EQ(mock_video_render_->GetReceivedFrames().size(), 1U);
  EXPECT_EQ(mock_avsync_controller_->GetMasterClock(), 5000);
}

}  // namespace player
}  // namespace ave
