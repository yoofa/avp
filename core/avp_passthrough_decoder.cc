/*
 * avp_passthrough_decoder.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_passthrough_decoder.h"

#include <cstdio>
#include <cstdlib>

#include "base/logging.h"
#include "media/audio/audio.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_frame.h"

#include "avp_audio_render.h"

#include "message_def.h"

using ave::media::MediaFrame;

namespace ave {
namespace player {

namespace {
// Maximum size of cached data before we stop fetching
const size_t kMaxCachedBytes = 200000;

// Optimal buffer size for power consumption
const size_t kAggregateBufferSizeBytes = 24 * 1024;

media::audio_config_t ConvertTrackInfoToAudioConfig(
    std::shared_ptr<MediaMeta> meta) {
  auto audio_info = &(meta->track_info()->audio());
  media::audio_config_t config = media::DefaultAudioConfig;

  if (audio_info) {
    config.sample_rate = static_cast<uint32_t>(audio_info->sample_rate_hz);
    config.channel_layout = audio_info->channel_layout;

    // Map codec to audio format
    switch (audio_info->codec_id) {
      case media::CodecId::AVE_CODEC_ID_PCM_S16LE:
      case media::CodecId::AVE_CODEC_ID_PCM_S16BE:
        config.format = media::AUDIO_FORMAT_PCM_16_BIT;
        break;
      case media::CodecId::AVE_CODEC_ID_PCM_S24LE:
      case media::CodecId::AVE_CODEC_ID_PCM_S24BE:
        config.format = media::AUDIO_FORMAT_PCM_24_BIT_PACKED;
        break;
      case media::CodecId::AVE_CODEC_ID_PCM_F32LE:
      case media::CodecId::AVE_CODEC_ID_PCM_F32BE:
        config.format = media::AUDIO_FORMAT_PCM_FLOAT;
        break;
      case media::CodecId::AVE_CODEC_ID_AAC:
        config.format = media::AUDIO_FORMAT_AAC_LC;
        config.offload_info.format = media::AUDIO_FORMAT_AAC_LC;
        config.offload_info.sample_rate = config.sample_rate;
        config.offload_info.channel_layout = config.channel_layout;
        config.offload_info.bit_width = audio_info->bits_per_sample;
        break;
      case media::CodecId::AVE_CODEC_ID_AC3:
        config.format = media::AUDIO_FORMAT_AC3;
        config.offload_info.format = media::AUDIO_FORMAT_AC3;
        config.offload_info.sample_rate = config.sample_rate;
        config.offload_info.channel_layout = config.channel_layout;
        break;
      case media::CodecId::AVE_CODEC_ID_DTS:
        config.format = media::AUDIO_FORMAT_DTS;
        config.offload_info.format = media::AUDIO_FORMAT_DTS;
        config.offload_info.sample_rate = config.sample_rate;
        config.offload_info.channel_layout = config.channel_layout;
        break;
      default:
        AVE_LOG(LS_WARNING)
            << "Unsupported codec: " << static_cast<int>(audio_info->codec_id)
            << ", using PCM as fallback";
        config.format = media::AUDIO_FORMAT_PCM_16_BIT;
        break;
    }

    // Calculate frame size
    if (config.format == media::AUDIO_FORMAT_PCM_16_BIT) {
      config.frame_size = 2 * ChannelLayoutToChannelCount(
                                  config.channel_layout);  // 2 bytes per sample
    } else if (config.format == media::AUDIO_FORMAT_PCM_24_BIT_PACKED) {
      config.frame_size = 3 * ChannelLayoutToChannelCount(
                                  config.channel_layout);  // 3 bytes per sample
    } else if (config.format == media::AUDIO_FORMAT_PCM_FLOAT) {
      config.frame_size = 4 * ChannelLayoutToChannelCount(
                                  config.channel_layout);  // 4 bytes per sample
    }
  }

  return config;
}

}  // namespace

AVPPassthroughDecoder::AVPPassthroughDecoder(
    const std::shared_ptr<Message>& notify,
    const std::shared_ptr<ContentSource>& source,
    const std::shared_ptr<AVPRender>& render)
    : AVPDecoderBase(notify, source, render),
      skip_rendering_until_media_time_us_(-1LL),
      reached_eos_(true),
      pending_buffers_to_drain_(0),
      total_bytes_(0),
      cached_bytes_(0),
      pending_audio_access_unit_(nullptr),
      pending_audio_err_(OK),
      buffer_generation_(0) {
  AVE_LOG(LS_VERBOSE) << "AVPPassthroughDecoder created";
}

AVPPassthroughDecoder::~AVPPassthroughDecoder() {
  AVE_LOG(LS_VERBOSE) << "~AVPPassthroughDecoder";
}

void AVPPassthroughDecoder::OnConfigure(
    const std::shared_ptr<MediaMeta>& format) {
  AVE_LOG(LS_VERBOSE) << "OnConfigure";

  cached_bytes_ = 0;
  pending_buffers_to_drain_ = 0;
  reached_eos_ = false;
  ++buffer_generation_;

  auto audio_render = std::dynamic_pointer_cast<AVPAudioRender>(avp_render_);
  audio_render->OpenAudioSink(ConvertTrackInfoToAudioConfig(format));

  // Request input buffers to start the decoding process
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::OnSetParameters(
    const std::shared_ptr<Message>& params) {
  AVE_LOG(LS_VERBOSE) << "OnSetParameters: " << params->what();
  // TODO: Implement parameter setting for passthrough decoder
}

void AVPPassthroughDecoder::OnSetVideoRender(
    const std::shared_ptr<VideoRender>& video_render) {
  // Passthrough decoder doesn't need video render
  AVE_LOG(LS_VERBOSE) << "OnSetVideoRender: ignored for passthrough decoder";
}

void AVPPassthroughDecoder::OnStart() {
  AVE_LOG(LS_VERBOSE) << "OnStart";
  paused_ = false;
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::OnPause() {
  AVE_LOG(LS_VERBOSE) << "OnPause";
  paused_ = true;
  // Passthrough decoder doesn't need special pause handling
}

void AVPPassthroughDecoder::OnResume() {
  AVE_LOG(LS_VERBOSE) << "OnResume";
  paused_ = false;
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::OnFlush() {
  AVE_LOG(LS_VERBOSE) << "OnFlush";
  DoFlush(true /* notifyComplete */);
}

void AVPPassthroughDecoder::OnShutdown() {
  AVE_LOG(LS_VERBOSE) << "OnShutdown";
  ++buffer_generation_;
  skip_rendering_until_media_time_us_ = -1;
  reached_eos_ = true;

  if (avp_render_) {
    avp_render_->Flush();
  }
}

bool AVPPassthroughDecoder::IsStaleReply(const std::shared_ptr<Message>& msg) {
  int32_t generation{};
  AVE_CHECK(msg->findInt32("generation", &generation));
  return generation != buffer_generation_;
}

bool AVPPassthroughDecoder::IsDoneFetching() const {
  AVE_LOG(LS_VERBOSE) << "IsDoneFetching: cached_bytes=" << cached_bytes_
                      << ", reached_eos=" << reached_eos_
                      << ", paused=" << paused_;

  return cached_bytes_ >= kMaxCachedBytes || reached_eos_ || paused_;
}

bool AVPPassthroughDecoder::DoRequestInputBuffers() {
  AVE_LOG(LS_VERBOSE) << "DoRequestInputBuffers: cached_bytes="
                      << cached_bytes_;
  status_t err = OK;
  while (!IsDoneFetching()) {
    std::shared_ptr<MediaFrame> packet;

    err = FetchInputData(packet);

    if (err != OK) {
      AVE_LOG(LS_VERBOSE)
          << "DoRequestInputBuffers: FetchInputData returned err=" << err;
      break;
    }

    OnInputBufferFilled(packet);
  }

  return (err == WOULD_BLOCK) && (source_->FeedMoreESData() == OK);
}

std::shared_ptr<MediaFrame> AVPPassthroughDecoder::AggregateBuffer(
    const std::shared_ptr<MediaFrame>& packet) {
  std::shared_ptr<MediaFrame> aggregate;

  if (packet == nullptr) {
    aggregate = aggregate_buffer_;
    aggregate_buffer_ = nullptr;
    return aggregate;
  }

  auto small_size = packet->size();
  if (aggregate_buffer_ == nullptr) {
    if (small_size < (kAggregateBufferSizeBytes / 3)) {
      return nullptr;
    }
    aggregate_buffer_ = MediaFrame::CreateShared(kAggregateBufferSizeBytes);
  }

  if (aggregate_buffer_ != nullptr) {
    auto small_timestamp = packet->meta()->pts();
    auto big_timestamp = aggregate_buffer_->meta()->pts();

    auto big_size = aggregate_buffer_->size();
    auto room_left = aggregate_buffer_->size() - big_size;

    // Should we save this small buffer for the next big buffer?
    // If the first small buffer did not have a timestamp then save
    // any buffer that does have a timestamp until the next big buffer.
    if ((small_size > room_left) ||
        (big_timestamp.IsInfinite() && (big_size > 0) &&
         small_timestamp.IsFinite())) {
      pending_audio_err_ = OK;
      pending_audio_access_unit_ = packet;
      aggregate = aggregate_buffer_;
      aggregate_buffer_ = nullptr;
    } else {
      // Grab time from first small buffer if available.
      if ((big_size == 0) && small_timestamp.IsFinite()) {
        aggregate_buffer_->meta()->SetPts(small_timestamp);
      }
      // Append small buffer to the bigger buffer.
      memcpy(const_cast<uint8_t*>(aggregate_buffer_->data() + big_size),
             packet->data(), small_size);
      big_size += small_size;
      aggregate_buffer_->setRange(aggregate_buffer_->offset(), big_size);

      AVE_LOG(LS_VERBOSE) << "feedDecoderInputData() smallSize = " << small_size
                          << ", bigSize = " << big_size
                          << ", capacity = " << aggregate_buffer_->size();
    }

  } else {
    aggregate = packet;
  }
  return aggregate;
}

status_t AVPPassthroughDecoder::DequeueAccessUnit(
    std::shared_ptr<MediaFrame>& packet) {
  status_t err = OK;
  if (pending_audio_access_unit_ != nullptr) {
    packet = pending_audio_access_unit_;
    pending_audio_access_unit_ = nullptr;
    err = pending_audio_err_;
    pending_audio_err_ = OK;
    AVE_LOG(LS_VERBOSE) << "feedDecoderInputData() use mPendingAudioAccessUnit";
  } else {
    err = source_->DequeueAccessUnit(MediaType::AUDIO, packet);
  }

  if (err == media::INFO_DISCONTINUITY || err == media::ERROR_END_OF_STREAM) {
    if (aggregate_buffer_ != nullptr) {
      pending_audio_err_ = err;
      pending_audio_access_unit_ = packet;
      packet.reset();
      AVE_LOG(LS_VERBOSE) << "return aggregated buffer and save err(=" << err
                          << ") for later";
      err = OK;
    }
  }
  return err;
}

status_t AVPPassthroughDecoder::FetchInputData(
    std::shared_ptr<MediaFrame>& packet) {
  do {
    status_t err = DequeueAccessUnit(packet);

    if (err == WOULD_BLOCK) {
      AVE_LOG(LS_VERBOSE)
          << "FetchInputData: DequeueAccessUnit returned WOULD_BLOCK";
      return err;
    }
    if (err != OK) {
      AVE_LOG(LS_VERBOSE) << "FetchInputData: DequeueAccessUnit returned err="
                          << err;
      if (err == media::ERROR_END_OF_STREAM) {
        reached_eos_ = true;
        AVE_LOG(LS_INFO) << "Passthrough decoder: End of stream reached";
      } else {
        ReportError(err);
      }
      return err;
    }
  } while (packet == nullptr);

  AVE_LOG(LS_VERBOSE) << "FetchInputData: got packet of size "
                      << packet->size();

  return OK;
}

void AVPPassthroughDecoder::OnInputBufferFilled(
    const std::shared_ptr<MediaFrame>& packet) {
  total_bytes_ += packet->size();
  AVE_LOG(LS_VERBOSE) << "OnInputBufferFilled: totalBytes=" << total_bytes_;
  if (reached_eos_) {
    return;
  }

  cached_bytes_ += packet->size();

  avp_render_->RenderFrame(
      packet, [this, size = packet->size()](bool rendered) {
        auto msg =
            std::make_shared<Message>(kWhatBufferConsumed, shared_from_this());
        msg->setInt32("generation", buffer_generation_);
        msg->setInt32("size", static_cast<int32_t>(size));
        msg->post();
      });

  pending_buffers_to_drain_++;
  AVE_LOG(LS_VERBOSE) << "OnInputBufferFilled: #ToDrain="
                      << pending_buffers_to_drain_
                      << ", cachedBytes=" << cached_bytes_;
}

void AVPPassthroughDecoder::OnBufferConsumed(int32_t size) {
  --pending_buffers_to_drain_;
  cached_bytes_ -= size;
  AVE_LOG(LS_VERBOSE) << "OnBufferConsumed: #ToDrain="
                      << pending_buffers_to_drain_ << ", consumed: " << size
                      << ", cachedBytes=" << cached_bytes_;
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::DoFlush(bool notifyComplete) {
  ++buffer_generation_;
  skip_rendering_until_media_time_us_ = -1;
  if (aggregate_buffer_) {
    // TODO: Fix MediaFrame interface
    // aggregate_buffer_->setRange(0, 0);
  }

  if (avp_render_) {
    avp_render_->Flush();
  }

  pending_buffers_to_drain_ = 0;
  cached_bytes_ = 0;
  reached_eos_ = false;
}

void AVPPassthroughDecoder::onMessageReceived(
    const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatBufferConsumed: {
      if (!IsStaleReply(msg)) {
        int32_t size{};
        AVE_CHECK(msg->findInt32("size", &size));
        OnBufferConsumed(size);
      }
      break;
    }

    default:
      AVPDecoderBase::onMessageReceived(msg);
      break;
  }
}

}  // namespace player
}  // namespace ave
