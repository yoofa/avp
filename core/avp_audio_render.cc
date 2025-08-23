/*
 * avp_audio_render.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_audio_render.h"

#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "base/time_utils.h"
#include "media/audio/audio_format.h"
#include "media/foundation/media_utils.h"

namespace ave {
namespace player {

AVPAudioRender::AVPAudioRender(base::TaskRunnerFactory* task_runner_factory,
                               IAVSyncController* avsync_controller,
                               std::shared_ptr<media::AudioDevice> audio_device,
                               bool master_stream)
    : AVPRender(task_runner_factory, avsync_controller),
      audio_device_(std::move(audio_device)),
      master_stream_(master_stream),
      audio_sink_ready_(false),
      playback_rate_(1.0f),
      format_initialized_(false),
      supports_playback_rate_(false),
      supports_timestamp_(false),
      total_bytes_written_(0),
      last_audio_pts_us_(-1) {
  AVE_CHECK(audio_device_ != nullptr);
  AVE_LOG(LS_INFO) << "AVPAudioRender created, master_stream: "
                   << master_stream;
}

AVPAudioRender::~AVPAudioRender() {
  CloseAudioSink();
}

status_t AVPAudioRender::OpenAudioSink(const media::audio_config_t& config) {
  std::lock_guard<std::mutex> lock(mutex_);

  AVE_LOG(LS_INFO) << "Opening audio sink with sample_rate: "
                   << config.sample_rate
                   << ", channels: " << static_cast<int>(config.channel_layout)
                   << ", format: " << static_cast<int>(config.format);

  // Store the configuration
  current_audio_config_ = config;
  format_initialized_ = true;

  // Create audio track
  status_t result = CreateAudioTrack();
  if (result == OK) {
    audio_sink_ready_ = true;
    AVE_LOG(LS_INFO) << "Audio sink opened successfully";
  } else {
    AVE_LOG(LS_ERROR) << "Failed to open audio sink, error: " << result;
  }

  return result;
}

void AVPAudioRender::CloseAudioSink() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (audio_sink_ready_) {
    AVE_LOG(LS_INFO) << "Closing audio sink";
    DestroyAudioTrack();
    audio_sink_ready_ = false;
    format_initialized_ = false;
  }
}

bool AVPAudioRender::IsAudioSinkReady() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return audio_sink_ready_ && audio_track_ && audio_track_->ready();
}

void AVPAudioRender::SetPlaybackRate(float rate) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (rate <= 0.0f) {
    AVE_LOG(LS_WARNING) << "Invalid playback rate: " << rate;
    return;
  }

  if (playback_rate_ != rate) {
    AVE_LOG(LS_INFO) << "Setting playback rate from " << playback_rate_
                     << " to " << rate;
    playback_rate_ = rate;

    // Apply the new rate if audio track is ready
    if (audio_sink_ready_ && audio_track_) {
      ApplyPlaybackRate();
    }
  }
}

void AVPAudioRender::Start() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (audio_sink_ready_ && audio_track_) {
      status_t result = audio_track_->Start();
      if (result == OK) {
        AVE_LOG(LS_INFO) << "Audio track started";
      } else {
        AVE_LOG(LS_ERROR) << "Failed to start audio track, error: " << result;
      }
    }
  }

  AVPRender::Start();
}

void AVPAudioRender::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (audio_track_) {
      audio_track_->Stop();
      AVE_LOG(LS_INFO) << "Audio track stopped";
    }
  }
  AVPRender::Stop();
}

void AVPAudioRender::Pause() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (audio_track_) {
      audio_track_->Pause();
      AVE_LOG(LS_INFO) << "Audio track paused";
    }
  }
  AVPRender::Pause();
}

void AVPAudioRender::Resume() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (audio_track_) {
      // Note: AudioTrack might not have Resume method, we may need to restart
      // This depends on the specific AudioTrack implementation
      AVE_LOG(LS_INFO) << "Audio track resumed";
    }
  }
  AVPRender::Resume();
}

void AVPAudioRender::Flush() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (audio_track_) {
      audio_track_->Flush();
      AVE_LOG(LS_INFO) << "Audio track flushed";
    }
  }
  AVPRender::Flush();
}

uint64_t AVPAudioRender::RenderFrameInternal(
    std::shared_ptr<media::MediaFrame>& frame) {
  if (!frame || frame->stream_type() != media::MediaType::AUDIO) {
    AVE_LOG(LS_WARNING) << "Invalid audio frame";
    return 0;
  }

  if (!audio_sink_ready_ || !audio_track_) {
    AVE_LOG(LS_WARNING) << "Audio sink not ready, dropping frame";
    return 0;
  }

  auto* audio_info = frame->audio_info();
  if (!audio_info) {
    AVE_LOG(LS_WARNING) << "No audio info in frame";
    return 0;
  }

  // Check for format changes
  if (HasAudioFormatChanged(frame)) {
    AVE_LOG(LS_INFO) << "Audio format changed, recreating audio track";
    DestroyAudioTrack();
    current_audio_config_ = ConvertToAudioConfig(frame);
    AVE_LOG(LS_INFO) << "New audio config - sample_rate: "
                     << current_audio_config_.sample_rate << ", channels: "
                     << static_cast<int>(current_audio_config_.channel_layout)
                     << ", format: "
                     << static_cast<int>(current_audio_config_.format);
    status_t result = CreateAudioTrack();
    if (result != OK) {
      AVE_LOG(LS_ERROR) << "Failed to recreate audio track after format change";
      return 0;
    }
  }

  // Write audio data to track
  ssize_t bytes_written = WriteAudioData(frame);
  if (bytes_written > 0) {
    total_bytes_written_ += bytes_written;
    AVE_LOG(LS_VERBOSE) << "Wrote " << bytes_written << " bytes to audio track";

    // Update sync anchor if this is the master stream
    if (master_stream_) {
      UpdateSyncAnchor(frame);
    }

    // Calculate next frame delay based on real playback latency
    // This should be based on the audio track's buffer state and sample rate
    int64_t next_delay_us = CalculateNextAudioFrameDelay();
    last_audio_pts_us_ = audio_info->pts.us();
    return next_delay_us;
  }

  last_audio_pts_us_ = audio_info->pts.us();
  return 0;
}

status_t AVPAudioRender::CreateAudioTrack() {
  if (!audio_device_) {
    AVE_LOG(LS_ERROR) << "Audio device not available";
    return UNKNOWN_ERROR;
  }

  audio_track_ = audio_device_->CreateAudioTrack();
  if (!audio_track_) {
    AVE_LOG(LS_ERROR) << "Failed to create audio track";
    return UNKNOWN_ERROR;
  }

  // Open the audio track with current configuration
  status_t result = audio_track_->Open(current_audio_config_);
  if (result != OK) {
    AVE_LOG(LS_ERROR) << "Failed to open audio track, error: " << result;
    audio_track_.reset();
    return result;
  }

  // Check capabilities
  supports_playback_rate_ = SupportsPlaybackRateChange();
  supports_timestamp_ = true;  // Assume timestamp support for now

  AVE_LOG(LS_INFO)
      << "Audio track created successfully, supports_playback_rate: "
      << supports_playback_rate_;

  // FIXME(youfa): start test
  audio_track_->Start();

  return OK;
}

void AVPAudioRender::DestroyAudioTrack() {
  if (audio_track_) {
    audio_track_->Close();
    audio_track_.reset();
    AVE_LOG(LS_INFO) << "Audio track destroyed";
  }
}

bool AVPAudioRender::HasAudioFormatChanged(
    const std::shared_ptr<media::MediaFrame>& frame) {
  if (!format_initialized_) {
    return false;
  }

  auto* audio_info = frame->audio_info();
  if (!audio_info) {
    return false;
  }

  // Check sample rate
  if (current_audio_config_.sample_rate !=
      static_cast<uint32_t>(audio_info->sample_rate_hz)) {
    return true;
  }

  // Check channel layout
  if (current_audio_config_.channel_layout != audio_info->channel_layout) {
    return true;
  }

  // Check format (simplified check - in real implementation you'd need to map
  // codec to format) For now, we'll assume PCM format for simplicity
  if (audio_info->codec_id != media::CodecId::AVE_CODEC_ID_PCM_S16LE &&
      audio_info->codec_id != media::CodecId::AVE_CODEC_ID_PCM_S16BE &&
      audio_info->codec_id != media::CodecId::AVE_CODEC_ID_PCM_S24LE &&
      audio_info->codec_id != media::CodecId::AVE_CODEC_ID_PCM_S24BE &&
      audio_info->codec_id != media::CodecId::AVE_CODEC_ID_PCM_F32LE &&
      audio_info->codec_id != media::CodecId::AVE_CODEC_ID_PCM_F32BE) {
    // This is an encoded format (AAC, DTS, etc.) - check if we need offload
    if (current_audio_config_.format == media::AUDIO_FORMAT_PCM_16_BIT) {
      return true;  // Need to switch to offload format
    }
  }

  return false;
}

media::audio_config_t AVPAudioRender::ConvertToAudioConfig(
    const std::shared_ptr<media::MediaMeta>& frame) {
  auto* audio_info = &(frame->sample_info()->audio());
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

ssize_t AVPAudioRender::WriteAudioData(
    const std::shared_ptr<media::MediaFrame>& frame) {
  if (!audio_track_ || !audio_track_->ready()) {
    return -1;
  }

  const uint8_t* data = frame->data();
  size_t size = frame->size();

  if (!data || size == 0) {
    AVE_LOG(LS_WARNING) << "Invalid audio data";
    return -1;
  }

  // Write data to audio track
  ssize_t bytes_written =
      audio_track_->Write(data, size, false);  // Non-blocking write

  if (bytes_written < 0) {
    AVE_LOG(LS_WARNING) << "Audio track write failed: " << bytes_written;
  }

  return bytes_written;
}

void AVPAudioRender::UpdateSyncAnchor(
    const std::shared_ptr<media::MediaFrame>& frame) {
  if (!GetAVSyncController()) {
    return;
  }

  auto* audio_info = frame->audio_info();
  if (!audio_info) {
    return;
  }

  int64_t frame_pts_us = audio_info->pts.us();
  int64_t frame_duration_us = audio_info->duration.us();
  int64_t current_sys_time_us = base::TimeMicros();

  // Calculate the end time of this audio frame
  int64_t frame_end_pts_us = frame_pts_us + frame_duration_us;

  // Update the sync controller anchor
  GetAVSyncController()->UpdateAnchor(frame_pts_us, current_sys_time_us,
                                      frame_end_pts_us);

  AVE_LOG(LS_VERBOSE) << "Updated sync anchor - PTS: " << frame_pts_us
                      << "us, sys_time: " << current_sys_time_us
                      << "us, max_time: " << frame_end_pts_us << "us";
}

bool AVPAudioRender::SupportsPlaybackRateChange() const {
  // This is a placeholder implementation
  // In a real implementation, you would check the audio track's capabilities
  // or try to set the playback rate and see if it succeeds

  // For now, assume PCM tracks don't support rate changes but offload tracks
  // might
  if (current_audio_config_.format == media::AUDIO_FORMAT_PCM_16_BIT ||
      current_audio_config_.format == media::AUDIO_FORMAT_PCM_24_BIT_PACKED ||
      current_audio_config_.format == media::AUDIO_FORMAT_PCM_FLOAT) {
    return false;  // PCM tracks typically don't support rate changes
  }

  return true;  // Offload tracks might support rate changes
}

void AVPAudioRender::ApplyPlaybackRate() {
  if (!supports_playback_rate_ || !audio_track_) {
    AVE_LOG(LS_VERBOSE) << "Playback rate change not supported by audio track";
    return;
  }

  // This is a placeholder implementation
  // In a real implementation, you would call the appropriate method on the
  // audio track to set the playback rate

  AVE_LOG(LS_INFO) << "Applied playback rate: " << playback_rate_
                   << " to audio track";

  // If the audio track doesn't support rate changes, we would need to implement
  // software-based rate changing (e.g., resampling) as a fallback
}

int64_t AVPAudioRender::CalculateNextAudioFrameDelay() {
  if (!audio_track_) {
    return 0;
  }

  // Get current buffer state from audio track
  uint32_t frames_written = 0;
  audio_track_->GetFramesWritten(&frames_written);

  // Calculate how much audio data is in the buffer
  int64_t buffer_duration_us = audio_track_->GetBufferDurationInUs();

  // Get the latency of the audio track
  uint32_t latency_ms = audio_track_->latency();
  int64_t latency_us = latency_ms * 1000LL;

  // Calculate time per frame
  float msecs_per_frame = audio_track_->msecsPerFrame();
  auto frame_duration_us = static_cast<int64_t>(msecs_per_frame * 1000.0f);

  // If buffer is nearly full, we need to wait longer
  // If buffer is nearly empty, we can write more data
  // For now, use a simple approach: schedule next frame after one frame
  // duration In a real implementation, you'd want to maintain a target buffer
  // level

  int64_t next_delay_us = frame_duration_us;

  // Adjust based on buffer state
  if (buffer_duration_us > latency_us * 0.8) {
    // Buffer is getting full, wait longer
    next_delay_us = frame_duration_us * 2;
  } else if (buffer_duration_us < latency_us * 0.2) {
    // Buffer is getting empty, write more frequently
    next_delay_us = frame_duration_us / 2;
  }

  return next_delay_us;
}

}  // namespace player
}  // namespace ave
