/*
 * avp_passthrough_decoder_unittest.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_passthrough_decoder.h"

#include <cstring>
#include <vector>

#include "test/gtest.h"

namespace ave {
namespace player {

namespace {

std::shared_ptr<media::MediaFrame> CreateTestAACPacket(
    const std::vector<uint8_t>& data) {
  auto packet =
      media::MediaFrame::CreateShared(data.size(), media::MediaType::AUDIO);
  auto* audio_info = packet->audio_info();
  audio_info->codec_id = media::CodecId::AVE_CODEC_ID_AAC;
  audio_info->sample_rate_hz = 44100;
  audio_info->channel_layout = media::CHANNEL_LAYOUT_STEREO;
  audio_info->bits_per_sample = 16;
  audio_info->pts = base::Timestamp::Micros(1000);
  audio_info->duration = base::TimeDelta::Micros(23220);
  std::memcpy(packet->data(), data.data(), data.size());
  packet->setRange(0, data.size());
  return packet;
}

media::audio_config_t CreateAacOffloadConfig() {
  media::audio_config_t config = media::DefaultAudioConfig;
  config.sample_rate = 44100;
  config.channel_layout = media::CHANNEL_LAYOUT_STEREO;
  config.format = media::AUDIO_FORMAT_AAC_LC;
  config.offload_info.format = media::AUDIO_FORMAT_AAC_LC;
  return config;
}

}  // namespace

class AVPPassthroughDecoderTest : public ::testing::Test {};

TEST_F(AVPPassthroughDecoderTest, PassesRawAacOffloadPacketUnchanged) {
  const std::vector<uint8_t> payload = {0x11, 0x22, 0x33, 0x44};

  auto prepared = AVPPassthroughDecoder::PreparePacketForOffload(
      CreateTestAACPacket(payload), CreateAacOffloadConfig(), nullptr);

  ASSERT_NE(prepared, nullptr);
  EXPECT_EQ(prepared->size(), payload.size());
  EXPECT_TRUE(std::equal(payload.begin(), payload.end(), prepared->data()));
}

TEST_F(AVPPassthroughDecoderTest, DoesNotDoubleWrapAdtsAacOffloadPacket) {
  const std::vector<uint8_t> adts_packet = {0xFF, 0xF1, 0x50, 0x80, 0x01, 0x7F,
                                            0xFC, 0x11, 0x22, 0x33, 0x44};

  auto prepared = AVPPassthroughDecoder::PreparePacketForOffload(
      CreateTestAACPacket(adts_packet), CreateAacOffloadConfig(), nullptr);

  ASSERT_NE(prepared, nullptr);
  EXPECT_EQ(prepared->size(), adts_packet.size());
  EXPECT_TRUE(
      std::equal(adts_packet.begin(), adts_packet.end(), prepared->data()));
}

}  // namespace player
}  // namespace ave
