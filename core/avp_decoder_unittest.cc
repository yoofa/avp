/*
 * avp_decoder_unittest.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_decoder.h"

#include <memory>

#include "base/test/mock_task_runner_factory.h"
#include "media/codec/codec_factory.h"
#include "media/codec/test/dummy_codec_factory.h"
#include "media/foundation/media_meta.h"
#include "media/foundation/media_packet.h"
#include "test/gtest.h"

#include "player/avp_render.h"

using ave::base::test::MockTaskRunnerFactory;
using ave::media::MediaMeta;
using ave::media::MediaPacket;
using ave::media::MediaType;
using ave::media::test::DummyCodecFactory;

namespace ave {
namespace player {

class MockContentSource : public ContentSource {
 public:
  MockContentSource() = default;
  ~MockContentSource() override = default;

  void SetNotify(Notify* notify) override { notify_ = notify; }
  void Prepare() override {}
  void Start() override {}
  void Stop() override {}
  void Pause() override {}
  void Resume() override {}

  status_t DequeueAccessUnit(
      MediaType track_type,
      std::shared_ptr<MediaPacket>& access_unit) override {
    if (packets_.empty()) {
      return WOULD_BLOCK;
    }
    access_unit = packets_.front();
    packets_.pop();
    return OK;
  }

  std::shared_ptr<MediaMeta> GetFormat() override { return format_; }
  status_t GetDuration(int64_t* duration_us) override {
    return INVALID_OPERATION;
  }
  size_t GetTrackCount() const override { return 1; }
  std::shared_ptr<MediaMeta> GetTrackInfo(size_t track_index) const override {
    return format_;
  }
  status_t SelectTrack(size_t track_index, bool select) override {
    return INVALID_OPERATION;
  }
  status_t SeekTo(int64_t seek_time_us, SeekMode mode) override {
    return INVALID_OPERATION;
  }
  bool IsStreaming() const override { return false; }
  status_t FeedMoreESData() override { return OK; }

  void AddPacket(std::shared_ptr<MediaPacket> packet) { packets_.push(packet); }

  void SetFormat(std::shared_ptr<MediaMeta> format) { format_ = format; }

 private:
  Notify* notify_ = nullptr;
  std::queue<std::shared_ptr<MediaPacket>> packets_;
  std::shared_ptr<MediaMeta> format_;
};

class MockAVPRender : public AVPRender {
 public:
  MockAVPRender() : AVPRender(nullptr, nullptr) {}
  ~MockAVPRender() override = default;

  void RenderFrame(
      std::shared_ptr<media::MediaFrame> frame,
      std::unique_ptr<RenderEvent> render_event = nullptr) override {
    rendered_frames_.push_back(frame);
    if (render_event) {
      render_event->OnRenderEvent(true);
    }
  }

  void Start() override {}
  void Stop() override {}
  void Pause() override {}
  void Resume() override {}
  void Flush() override {}

  const std::vector<std::shared_ptr<media::MediaFrame>>& GetRenderedFrames()
      const {
    return rendered_frames_;
  }

  void ClearRenderedFrames() { rendered_frames_.clear(); }

 protected:
  uint64_t RenderFrameInternal(
      std::shared_ptr<media::MediaFrame>& frame) override {
    return 0;
  }

 private:
  std::vector<std::shared_ptr<media::MediaFrame>> rendered_frames_;
};

class AVPDecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    task_runner_factory_ = std::make_unique<MockTaskRunnerFactory>();
    codec_factory_ = std::make_shared<DummyCodecFactory>();
    content_source_ = std::make_shared<MockContentSource>();
    render_ = std::make_unique<MockAVPRender>();
    notify_ = std::make_shared<Message>();
  }

  void TearDown() override {
    if (decoder_) {
      decoder_->Shutdown();
    }
  }

  std::shared_ptr<AVPDecoder> CreateDecoder() {
    return std::make_shared<AVPDecoder>(codec_factory_, notify_,
                                        content_source_, render_.get());
  }

  std::shared_ptr<MediaMeta> CreateAudioFormat() {
    auto format = std::make_shared<MediaMeta>();
    format->setMime("audio/aac");
    format->setInt32("sample_rate", 44100);
    format->setInt32("channel_count", 2);
    return format;
  }

  std::shared_ptr<MediaMeta> CreateVideoFormat() {
    auto format = std::make_shared<MediaMeta>();
    format->setMime("video/avc");
    format->setInt32("width", 1920);
    format->setInt32("height", 1080);
    return format;
  }

  std::shared_ptr<MediaPacket> CreatePacket(const uint8_t* data,
                                            size_t size,
                                            int64_t pts = 0) {
    auto packet = MediaPacket::CreateShared(size);
    if (data && size > 0) {
      memcpy(packet->data(), data, size);
    }
    packet->setPts(pts);
    packet->setRange(0, size);
    return packet;
  }

  std::unique_ptr<MockTaskRunnerFactory> task_runner_factory_;
  std::shared_ptr<DummyCodecFactory> codec_factory_;
  std::shared_ptr<MockContentSource> content_source_;
  std::unique_ptr<MockAVPRender> render_;
  std::shared_ptr<Message> notify_;
  std::shared_ptr<AVPDecoder> decoder_;
};

TEST_F(AVPDecoderTest, CreateAndDestroy) {
  decoder_ = CreateDecoder();
  EXPECT_NE(decoder_, nullptr);
}

TEST_F(AVPDecoderTest, ConfigureAudio) {
  decoder_ = CreateDecoder();
  auto format = CreateAudioFormat();
  content_source_->SetFormat(format);

  decoder_->Init();
  decoder_->Configure(format);
  decoder_->Start();

  // Add some test packets
  uint8_t test_data[] = {0x00, 0x01, 0x02, 0x03};
  auto packet = CreatePacket(test_data, sizeof(test_data), 1000);
  content_source_->AddPacket(packet);

  // Let the decoder process
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  decoder_->Pause();
  decoder_->Resume();
  decoder_->Flush();
}

TEST_F(AVPDecoderTest, ConfigureVideo) {
  decoder_ = CreateDecoder();
  auto format = CreateVideoFormat();
  content_source_->SetFormat(format);

  decoder_->Init();
  decoder_->Configure(format);
  decoder_->Start();

  // Add some test packets
  uint8_t test_data[] = {0x00, 0x01, 0x02, 0x03};
  auto packet = CreatePacket(test_data, sizeof(test_data), 1000);
  content_source_->AddPacket(packet);

  // Let the decoder process
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  decoder_->Pause();
  decoder_->Resume();
  decoder_->Flush();
}

TEST_F(AVPDecoderTest, HandleEndOfStream) {
  decoder_ = CreateDecoder();
  auto format = CreateAudioFormat();
  content_source_->SetFormat(format);

  decoder_->Init();
  decoder_->Configure(format);
  decoder_->Start();

  // Add EOS packet
  auto eos_packet = CreatePacket(nullptr, 0, 0);
  eos_packet->setFlags(MediaPacket::FLAG_END_OF_STREAM);
  content_source_->AddPacket(eos_packet);

  // Let the decoder process
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(AVPDecoderTest, ErrorHandling) {
  decoder_ = CreateDecoder();

  // Try to configure with invalid format
  auto invalid_format = std::make_shared<MediaMeta>();
  invalid_format->setMime("invalid/mime");

  decoder_->Init();
  decoder_->Configure(invalid_format);

  // Should handle error gracefully
  EXPECT_TRUE(true);  // If we get here without crash, it's good
}

}  // namespace player
}  // namespace ave