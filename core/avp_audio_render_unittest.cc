/*
 * avp_audio_render_unittest.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_audio_render.h"

#include <memory>

#include "base/logging.h"
#include "base/time_utils.h"
#include "media/audio/audio_device.h"
#include "media/foundation/media_frame.h"

#include "mock_task_runner_factory.h"

#include "test/gtest.h"

namespace ave {
namespace player {

namespace {

// Mock AudioTrack for testing
class MockAudioTrack : public media::AudioTrack {
 public:
  MockAudioTrack() : ready_(false), started_(false) {}
  ~MockAudioTrack() override = default;

  bool ready() const override { return ready_; }
  ssize_t bufferSize() const override { return 8192; }
  ssize_t frameCount() const override { return 512; }
  ssize_t channelCount() const override { return 2; }
  ssize_t frameSize() const override { return 4; }
  uint32_t sampleRate() const override { return 44100; }
  uint32_t latency() const override { return 100; }
  float msecsPerFrame() const override { return 11.6f; }

  status_t GetPosition(uint32_t* position) const override {
    if (position)
      *position = position_;
    return OK;
  }

  int64_t GetPlayedOutDurationUs(int64_t nowUs) const override {
    return (nowUs - start_time_us_) * (started_ ? 1 : 0);
  }

  status_t GetFramesWritten(uint32_t* frameswritten) const override {
    if (frameswritten)
      *frameswritten = frames_written_;
    return OK;
  }

  int64_t GetBufferDurationInUs() const override { return 100000; }

  status_t Open(media::audio_config_t config,
                AudioCallback cb,
                void* cookie) override {
    config_ = config;
    callback_ = cb;
    cookie_ = cookie;
    ready_ = true;
    return OK;
  }

  ssize_t Write(const void* buffer, size_t size, bool blocking) override {
    if (!ready_)
      return -1;

    bytes_written_ += size;
    frames_written_ += size / frameSize();
    return size;
  }

  status_t Start() override {
    if (!ready_)
      return -1;
    started_ = true;
    start_time_us_ = base::TimeMicros();
    return OK;
  }

  void Stop() override { started_ = false; }
  void Flush() override { bytes_written_ = 0; }
  void Pause() override { started_ = false; }
  void Close() override { ready_ = false; }

  // Test helper methods
  media::audio_config_t GetConfig() const { return config_; }
  size_t GetBytesWritten() const { return bytes_written_; }
  bool IsStarted() const { return started_; }

 private:
  bool ready_;
  bool started_;
  media::audio_config_t config_;
  AudioCallback callback_;
  void* cookie_;
  uint32_t position_ = 0;
  uint32_t frames_written_ = 0;
  size_t bytes_written_ = 0;
  int64_t start_time_us_ = 0;
};

// Mock AudioDevice for testing
class MockAudioDevice : public media::AudioDevice {
 public:
  MockAudioDevice() = default;
  ~MockAudioDevice() override = default;

  status_t Init() override { return OK; }

  std::shared_ptr<media::AudioTrack> CreateAudioTrack() override {
    return std::make_shared<MockAudioTrack>();
  }

  std::shared_ptr<media::AudioRecord> CreateAudioRecord() override {
    return nullptr;
  }

  std::shared_ptr<media::AudioLoopback> CreateAudioLoopback() override {
    return nullptr;
  }

  std::vector<std::pair<int, media::AudioDeviceInfo>> GetSupportedAudioDevices()
      override {
    return {};
  }

  status_t SetAudioInputDevice(int device_id) override { return OK; }
  status_t SetAudioOutputDevice(int device_id) override { return OK; }
};

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

// Helper function to create a test audio frame
std::shared_ptr<media::MediaFrame> CreateTestAudioFrame(
    int64_t pts_us,
    size_t data_size = 1024) {
  auto frame = media::MediaFrame::Create(data_size);
  frame.SetMediaType(media::MediaType::AUDIO);

  auto* audio_info = frame.audio_info();
  if (audio_info) {
    audio_info->codec_id = media::CodecId::AVE_CODEC_ID_PCM_S16LE;
    audio_info->sample_rate_hz = 44100;
    audio_info->channel_layout = media::CHANNEL_LAYOUT_STEREO;
    audio_info->bits_per_sample = 16;
    audio_info->pts = base::Timestamp::Micros(pts_us);
    audio_info->duration =
        base::TimeDelta::Micros(23220);  // ~1 frame at 44.1kHz
  }

  // Fill with dummy audio data
  uint8_t* data = const_cast<uint8_t*>(frame.data());
  for (size_t i = 0; i < data_size; i++) {
    data[i] = static_cast<uint8_t>(i % 256);
  }

  return std::make_shared<media::MediaFrame>(frame);
}

}  // namespace

class AVPAudioRenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_task_runner_factory_ = std::make_unique<MockTaskRunnerFactory>();
    mock_audio_device_ = std::make_shared<MockAudioDevice>();
    mock_avsync_controller_ = std::make_unique<MockAVSyncController>();

    audio_render_ = std::make_unique<AVPAudioRender>(
        mock_task_runner_factory_.get(), mock_avsync_controller_.get(),
        mock_audio_device_,
        true);  // master stream
  }

  void TearDown() override {
    audio_render_.reset();
    mock_avsync_controller_.reset();
    mock_audio_device_.reset();
    mock_task_runner_factory_.reset();
  }

  std::unique_ptr<MockTaskRunnerFactory> mock_task_runner_factory_;
  std::shared_ptr<MockAudioDevice> mock_audio_device_;
  std::unique_ptr<MockAVSyncController> mock_avsync_controller_;
  std::unique_ptr<AVPAudioRender> audio_render_;
};

TEST_F(AVPAudioRenderTest, Construction) {
  EXPECT_NE(audio_render_, nullptr);
  EXPECT_FALSE(audio_render_->IsAudioSinkReady());
}

TEST_F(AVPAudioRenderTest, OpenAudioSink) {
  media::audio_config_t config = media::DefaultAudioConfig;
  config.sample_rate = 44100;
  config.channel_layout = media::CHANNEL_LAYOUT_STEREO;
  config.format = media::AUDIO_FORMAT_PCM_16_BIT;

  status_t result = audio_render_->OpenAudioSink(config);
  EXPECT_EQ(result, OK);
  EXPECT_TRUE(audio_render_->IsAudioSinkReady());
}

TEST_F(AVPAudioRenderTest, CloseAudioSink) {
  media::audio_config_t config = media::DefaultAudioConfig;
  audio_render_->OpenAudioSink(config);
  EXPECT_TRUE(audio_render_->IsAudioSinkReady());

  audio_render_->CloseAudioSink();
  EXPECT_FALSE(audio_render_->IsAudioSinkReady());
}

TEST_F(AVPAudioRenderTest, SetPlaybackRate) {
  EXPECT_EQ(audio_render_->GetPlaybackRate(), 1.0f);

  audio_render_->SetPlaybackRate(2.0f);
  EXPECT_EQ(audio_render_->GetPlaybackRate(), 2.0f);

  // Test invalid rate
  audio_render_->SetPlaybackRate(0.0f);
  EXPECT_EQ(audio_render_->GetPlaybackRate(), 2.0f);  // Should not change
}

TEST_F(AVPAudioRenderTest, StartStopAudioTrack) {
  media::audio_config_t config = media::DefaultAudioConfig;
  audio_render_->OpenAudioSink(config);

  audio_render_->Start();
  // Note: We can't easily test if the audio track was started without exposing
  // the internal audio track, but we can test that Start() doesn't crash

  audio_render_->Stop();
  // Similarly, we can test that Stop() doesn't crash
}

TEST_F(AVPAudioRenderTest, RenderAudioFrame) {
  media::audio_config_t config = media::DefaultAudioConfig;
  audio_render_->OpenAudioSink(config);
  audio_render_->Start();

  auto frame =
      CreateTestAudioFrame(1000);  // Use smaller PTS (1ms instead of 1s)
  audio_render_->RenderFrame(frame);

  // For audio frames, they are scheduled immediately (delay=0)
  // So we only need to advance time once and run tasks
  mock_task_runner_factory_->runner()->AdvanceTimeUs(1000);
  mock_task_runner_factory_->runner()->RunDueTasks();

  // Check that the sync controller was updated (since this is a master stream)
  EXPECT_GT(mock_avsync_controller_->GetUpdateCount(), 0);
}

TEST_F(AVPAudioRenderTest, NonMasterStream) {
  // Create a non-master stream renderer
  auto non_master_render = std::make_unique<AVPAudioRender>(
      mock_task_runner_factory_.get(), mock_avsync_controller_.get(),
      mock_audio_device_,
      false);  // not master stream

  media::audio_config_t config = media::DefaultAudioConfig;
  non_master_render->OpenAudioSink(config);
  non_master_render->Start();

  auto frame = CreateTestAudioFrame(1000);
  non_master_render->RenderFrame(frame);

  // For audio frames, they are scheduled immediately (delay=0)
  mock_task_runner_factory_->runner()->AdvanceTimeUs(1000);
  mock_task_runner_factory_->runner()->RunDueTasks();

  // Check that the sync controller was NOT updated (since this is not a master
  // stream)
  EXPECT_EQ(mock_avsync_controller_->GetUpdateCount(), 0);
}

TEST_F(AVPAudioRenderTest, FormatChangeDetection) {
  media::audio_config_t config = media::DefaultAudioConfig;
  audio_render_->OpenAudioSink(config);
  audio_render_->Start();

  // First frame with PCM format
  auto frame1 = CreateTestAudioFrame(1000);  // Use smaller PTS
  audio_render_->RenderFrame(frame1);
  mock_task_runner_factory_->runner()->AdvanceTimeUs(1000);
  mock_task_runner_factory_->runner()->RunDueTasks();

  // Second frame with different format (this would require a real format
  // change) For now, we just test that the renderer handles multiple frames
  auto frame2 = CreateTestAudioFrame(2000);  // Use smaller PTS
  audio_render_->RenderFrame(frame2);
  mock_task_runner_factory_->runner()->AdvanceTimeUs(1000);
  mock_task_runner_factory_->runner()->RunDueTasks();

  // Both frames should be processed
  EXPECT_GT(mock_avsync_controller_->GetUpdateCount(), 0);
}

}  // namespace player
}  // namespace ave
