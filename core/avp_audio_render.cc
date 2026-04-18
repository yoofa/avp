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

namespace {
constexpr int64_t kCompressedStartPrerollUs = 250000;
}

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
    audio_track_started_ = false;
    if (IsRunningLocked() && !IsPausedLocked() && audio_track_) {
      if (IsCompressedOutputLocked()) {
        AVE_LOG(LS_INFO)
            << "Audio sink opened while renderer already running, deferring "
               "compressed track start until preroll";
      } else {
        status_t start_result =
            StartAudioTrackLocked("audio sink opened while renderer running");
        if (start_result != OK) {
          audio_sink_ready_ = false;
          return start_result;
        }
      }
    }
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

    // Reset audio pacing state on every fresh start so the new session
    // measures ahead-ness relative to its own start time.
    audio_pacing_start_time_us_ = 0;
    audio_pacing_start_pts_us_ = 0;
    passthrough_media_start_pts_us_ = 0;
    passthrough_last_written_end_pts_us_ = 0;
    passthrough_media_start_pts_valid_ = false;
    passthrough_position_started_ = false;
    ++passthrough_position_poll_generation_;
    passthrough_position_poll_pending_ = false;
    audio_track_started_ = false;
    last_position_log_time_us_ = 0;
    last_played_frames_ = 0;

    if (audio_sink_ready_ && audio_track_) {
      if (IsCompressedOutputLocked()) {
        AVE_LOG(LS_INFO)
            << "Deferring compressed track start until preroll is queued";
      } else {
        StartAudioTrackLocked("renderer start");
      }
    }
  }

  AVPRender::Start();
}

void AVPAudioRender::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (audio_track_) {
      ++passthrough_position_poll_generation_;
      passthrough_position_poll_pending_ = false;
      if (audio_track_started_) {
        audio_track_->Stop();
        AVE_LOG(LS_INFO) << "Audio track stopped";
      }
      audio_track_started_ = false;
    }
  }
  AVPRender::Stop();
}

void AVPAudioRender::Pause() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (audio_track_) {
      ++passthrough_position_poll_generation_;
      passthrough_position_poll_pending_ = false;
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
      if (audio_track_started_) {
        status_t result = audio_track_->Start();
        if (result == OK) {
          AVE_LOG(LS_INFO) << "Audio track resumed";
          if (IsCompressedOutputLocked()) {
            MaybeScheduleCompressedPositionPollLocked();
          }
        } else {
          AVE_LOG(LS_ERROR)
              << "Failed to resume audio track, error: " << result;
        }
      } else if (IsCompressedOutputLocked()) {
        AVE_LOG(LS_INFO)
            << "Resume requested before compressed track start; waiting for "
               "preroll";
      }
    }
  }
  AVPRender::Resume();
}

void AVPAudioRender::Flush() {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Reset audio pacing so the next burst of frames after a seek/flush is
    // measured from the new position, not the old one.
    audio_pacing_start_time_us_ = 0;
    audio_pacing_start_pts_us_ = 0;
    passthrough_media_start_pts_us_ = 0;
    passthrough_last_written_end_pts_us_ = 0;
    passthrough_media_start_pts_valid_ = false;
    passthrough_position_started_ = false;
    ++passthrough_position_poll_generation_;
    passthrough_position_poll_pending_ = false;
    audio_track_started_ = false;
    last_position_log_time_us_ = 0;
    last_played_frames_ = 0;

    if (audio_track_) {
      audio_track_->Flush();
      AVE_LOG(LS_INFO) << "Audio track flushed";
    }
  }
  AVPRender::Flush();
}

uint64_t AVPAudioRender::RenderFrameInternal(
    std::shared_ptr<media::MediaFrame>& frame,
    bool& consumed) {
  AVE_LOG(LS_INFO) << "RenderFrameInternal[AUDIO]: enter, "
                   << "frame_size=" << (frame ? frame->size() : 0)
                   << ", format_initialized=" << format_initialized_
                   << ", audio_sink_ready=" << audio_sink_ready_
                   << ", has_audio_track=" << (audio_track_ != nullptr)
                   << ", has_cached=" << (cached_frame_ != nullptr);

  if (!frame || frame->stream_type() != media::MediaType::AUDIO) {
    AVE_LOG(LS_WARNING) << "Invalid audio frame";
    return 0;
  }

  auto* audio_info = frame->audio_info();
  if (!audio_info) {
    AVE_LOG(LS_WARNING) << "No audio info in frame";
    return 0;
  }

  // Initialize audio track from the first frame if not yet done
  if (!format_initialized_) {
    current_audio_config_ = ConvertToAudioConfig(frame);
    AVE_LOG(LS_INFO) << "RenderFrameInternal[AUDIO]: lazy init - sample_rate: "
                     << current_audio_config_.sample_rate << ", channels: "
                     << static_cast<int>(current_audio_config_.channel_layout);
    status_t result = CreateAudioTrack();
    if (result != OK) {
      AVE_LOG(LS_ERROR) << "Failed to create audio track from first frame";
      return 0;
    }
    format_initialized_ = true;
    audio_sink_ready_ = true;
    audio_track_started_ = false;
    if (IsCompressedOutputLocked()) {
      AVE_LOG(LS_INFO)
          << "RenderFrameInternal[AUDIO]: deferring compressed track start "
             "after lazy init";
    } else {
      StartAudioTrackLocked("lazy init");
    }
  }

  if (!audio_sink_ready_ || !audio_track_) {
    AVE_LOG(LS_WARNING) << "Audio sink not ready, dropping frame";
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
    audio_track_started_ = false;
    if (IsCompressedOutputLocked()) {
      AVE_LOG(LS_INFO)
          << "RenderFrameInternal[AUDIO]: deferring compressed track start "
             "after format change";
    } else {
      StartAudioTrackLocked("format change");
    }
  }

  // Track whether we consumed the incoming frame in this call. Used to
  // correctly revert the consumption if the write fails entirely.
  bool consumed_frame_this_call = false;
  if (!cached_frame_) {
    cached_frame_ = frame;
    consumed = true;
    consumed_frame_this_call = true;
    AVE_LOG(LS_INFO) << "RenderFrameInternal[AUDIO]: consumed new frame, "
                     << "size=" << cached_frame_->size();
  } else {
    consumed = false;
    AVE_LOG(LS_INFO) << "RenderFrameInternal[AUDIO]: retrying cached frame, "
                     << "remaining=" << cached_frame_->size();
  }

  auto frame_to_write = cached_frame_;
  auto* written_audio_info =
      frame_to_write ? frame_to_write->audio_info() : audio_info;
  const size_t requested_size = frame_to_write ? frame_to_write->size() : 0;
  const bool is_compressed = (current_audio_config_.format & 0xFF000000u) != 0;

  if (!is_compressed && frame_to_write) {
    static int s_pcm_debug_frames = 0;
    if (s_pcm_debug_frames < 8) {
      const uint8_t* pcm = frame_to_write->data();
      const size_t inspect_bytes =
          requested_size < 256 ? requested_size : static_cast<size_t>(256);
      int nonzero_bytes = 0;
      int sample_sum = 0;
      for (size_t i = 0; i < inspect_bytes; ++i) {
        const int value = static_cast<int>(static_cast<int8_t>(pcm[i]));
        sample_sum += value >= 0 ? value : -value;
        if (pcm[i] != 0) {
          ++nonzero_bytes;
        }
      }
      AVE_LOG(LS_INFO) << "RenderFrameInternal[AUDIO]: pcm probe bytes="
                       << inspect_bytes << " nonzero=" << nonzero_bytes
                       << " abs_sum=" << sample_sum;
      ++s_pcm_debug_frames;
    }
  }

  // Write audio data to track.  Single-shot write with a short timeout so
  // the caller's mutex is not held for a long time.  Partial writes are
  // handled via cached_frame_ and a 0-delay re-schedule.
  ssize_t bytes_written = WriteAudioData(frame_to_write);
  if (bytes_written > 0) {
    total_bytes_written_ += bytes_written;
    const bool full_frame_written =
        static_cast<size_t>(bytes_written) >= requested_size;

    if (is_compressed && full_frame_written && written_audio_info &&
        written_audio_info->pts.IsFinite()) {
      const int64_t written_duration_us =
          written_audio_info->duration.IsFinite()
              ? written_audio_info->duration.us()
              : 0;
      const int64_t written_end_pts_us =
          written_audio_info->pts.us() + written_duration_us;
      if (written_end_pts_us > passthrough_last_written_end_pts_us_) {
        passthrough_last_written_end_pts_us_ = written_end_pts_us;
      }
    }

    if (is_compressed) {
      MaybeStartCompressedTrackLocked(frame_to_write, bytes_written,
                                      requested_size);
      if (audio_track_started_) {
        MaybeScheduleCompressedPositionPollLocked();
      }
    }

    if (full_frame_written) {
      if (master_stream_) {
        UpdateSyncAnchor(frame_to_write);
      }
      cached_frame_.reset();
    } else {
      auto new_offset =
          cached_frame_->data() - cached_frame_->base() + bytes_written;
      auto new_size = cached_frame_->size() - bytes_written;
      cached_frame_->setRange(new_offset, new_size);
    }
  } else if (bytes_written == 0) {
    // AAudio buffer full or temporarily stalled — write timed out.
    if (consumed_frame_this_call) {
      // We just took this frame from the queue but wrote nothing.
      // Revert the consumption so the frame remains in the queue for retry:
      // release our cached reference (queue still holds it).
      cached_frame_.reset();
      consumed = false;
    }
    // else: cached_frame_ has partial data from a previous write — keep it.
    AVE_LOG(LS_INFO) << "RenderFrameInternal[AUDIO]: write stalled, "
                     << "consumed_this_call=" << consumed_frame_this_call
                     << ", retry in 5ms";
    return 5000;
  } else {
    // Write error — drop the frame to avoid stalling the pipeline
    AVE_LOG(LS_WARNING) << "Audio write error: " << bytes_written
                        << ", dropping frame";
    cached_frame_.reset();
    consumed = true;
    return 0;
  }

  AVE_LOG(LS_INFO) << "RenderFrameInternal[AUDIO]: wrote " << bytes_written
                   << " bytes, total=" << total_bytes_written_
                   << ", cached=" << (cached_frame_ ? "partial" : "done");
  if (!is_compressed && written_audio_info &&
      written_audio_info->pts.IsFinite()) {
    last_audio_pts_us_ = written_audio_info->pts.us();
  }

  // PCM pacing: some sinks accept PCM writes much faster than real playout.
  // Compressed/offload audio uses played-frame position for the audio clock,
  // so pre-roll should drain into the HAL as fast as the sink accepts it.
  if (!is_compressed && !cached_frame_ && written_audio_info &&
      written_audio_info->pts.IsFinite()) {
    int64_t frame_pts_us = written_audio_info->pts.us();
    int64_t frame_duration_us = written_audio_info->duration.IsFinite()
                                    ? written_audio_info->duration.us()
                                    : 0;
    int64_t frame_end_pts_us = frame_pts_us + frame_duration_us;
    int64_t now_us = base::TimeMicros();

    if (audio_pacing_start_time_us_ == 0) {
      audio_pacing_start_time_us_ = now_us;
      audio_pacing_start_pts_us_ = frame_end_pts_us;
    }

    // How far ahead of "expected real-time" are we?
    int64_t expected_pts_us =
        audio_pacing_start_pts_us_ + (now_us - audio_pacing_start_time_us_);
    int64_t ahead_us = frame_end_pts_us - expected_pts_us;

    constexpr int64_t kMaxAheadUs = 80000;  // ~2 audio callbacks at 48 kHz PCM
    if (ahead_us > kMaxAheadUs) {
      int64_t delay_us = ahead_us - kMaxAheadUs;
      AVE_LOG(LS_VERBOSE) << "Audio pacing: ahead=" << ahead_us / 1000
                          << "ms, delaying " << delay_us / 1000 << "ms";
      return static_cast<uint64_t>(delay_us);
    }
  }

  return 0;
}

bool AVPAudioRender::IsCompressedOutputLocked() const {
  return (current_audio_config_.format & 0xFF000000u) != 0;
}

status_t AVPAudioRender::StartAudioTrackLocked(const char* reason) {
  if (!audio_track_) {
    AVE_LOG(LS_ERROR) << "StartAudioTrackLocked: no audio track";
    return INVALID_OPERATION;
  }
  if (audio_track_started_) {
    return OK;
  }
  status_t result = audio_track_->Start();
  if (result == OK) {
    audio_track_started_ = true;
    AVE_LOG(LS_INFO) << "Audio track started: " << reason;
    if (IsCompressedOutputLocked()) {
      MaybeScheduleCompressedPositionPollLocked();
    }
  } else {
    AVE_LOG(LS_ERROR) << "Failed to start audio track (" << reason
                      << "), error: " << result;
  }
  return result;
}

void AVPAudioRender::MaybeStartCompressedTrackLocked(
    const std::shared_ptr<media::MediaFrame>& frame,
    ssize_t bytes_written,
    size_t requested_size) {
  if (!IsCompressedOutputLocked() || audio_track_started_ || !audio_track_ ||
      bytes_written <= 0 || !frame) {
    return;
  }

  auto* audio_info = frame->audio_info();
  if (!audio_info || !audio_info->pts.IsFinite()) {
    return;
  }

  if (!passthrough_media_start_pts_valid_) {
    passthrough_media_start_pts_us_ = audio_info->pts.us();
    passthrough_media_start_pts_valid_ = true;
  }

  const int64_t frame_duration_us =
      audio_info->duration.IsFinite() ? audio_info->duration.us() : 0;
  int64_t preroll_us = audio_info->pts.us() + frame_duration_us -
                       passthrough_media_start_pts_us_;
  if (preroll_us < 0) {
    preroll_us = 0;
  }

  const bool sink_backpressured =
      static_cast<size_t>(bytes_written) < requested_size;
  if (!sink_backpressured && preroll_us < kCompressedStartPrerollUs) {
    return;
  }

  AVE_LOG(LS_INFO) << "Starting compressed track after preroll_ms="
                   << preroll_us / 1000
                   << " backpressured=" << sink_backpressured;
  StartAudioTrackLocked("compressed preroll");
}

void AVPAudioRender::MaybeScheduleCompressedPositionPollLocked(
    uint32_t delay_us) {
  if (!task_runner_ || !master_stream_ || !audio_track_ ||
      !audio_track_started_ || !IsCompressedOutputLocked() ||
      !IsRunningLocked() || IsPausedLocked() ||
      passthrough_position_poll_pending_) {
    return;
  }

  passthrough_position_poll_pending_ = true;
  const int32_t generation = passthrough_position_poll_generation_;
  task_runner_->PostDelayedTask(
      [this, generation]() { OnCompressedPositionPoll(generation); }, delay_us);
}

void AVPAudioRender::OnCompressedPositionPoll(int32_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  passthrough_position_poll_pending_ = false;

  if (generation != passthrough_position_poll_generation_ || !master_stream_ ||
      !audio_track_ || !audio_track_started_ || !IsCompressedOutputLocked() ||
      !IsRunningLocked() || IsPausedLocked()) {
    return;
  }

  UpdateCompressedSyncAnchorLocked(passthrough_last_written_end_pts_us_);
  MaybeScheduleCompressedPositionPollLocked(10000);
}

status_t AVPAudioRender::CreateAudioTrack() {
  if (!audio_device_) {
    AVE_LOG(LS_ERROR) << "Audio device not available";
    return UNKNOWN_ERROR;
  }

  AVE_LOG(LS_INFO) << "CreateAudioTrack: creating audio track object";
  audio_track_ = audio_device_->CreateAudioTrack();
  if (!audio_track_) {
    AVE_LOG(LS_ERROR) << "Failed to create audio track";
    return UNKNOWN_ERROR;
  }

  // Open the audio track with current configuration
  AVE_LOG(LS_INFO) << "CreateAudioTrack: calling Open() with sr="
                   << current_audio_config_.sample_rate << " ch="
                   << static_cast<int>(current_audio_config_.channel_layout)
                   << " fmt=" << static_cast<int>(current_audio_config_.format);
  status_t result = audio_track_->Open(current_audio_config_);
  AVE_LOG(LS_INFO) << "CreateAudioTrack: Open() returned " << result;
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

  // Remove the FIXME test start: do NOT start the track here.
  // The track will be started by AVPAudioRender::Start() (for pre-opened
  // sinks) or immediately after lazy creation in RenderFrameInternal.

  return OK;
}

void AVPAudioRender::DestroyAudioTrack() {
  if (audio_track_) {
    ++passthrough_position_poll_generation_;
    passthrough_position_poll_pending_ = false;
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

  // Passthrough (compressed) audio frames may not carry per-sample audio
  // parameters — their sample_rate_hz and channel_layout remain at default
  // uninitialized values.  Skip format-change detection when the frame lacks
  // valid metadata to avoid falsely destroying a working audio track.
  if (audio_info->sample_rate_hz <= 0 ||
      audio_info->channel_layout == media::CHANNEL_LAYOUT_NONE) {
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
    AVE_LOG(LS_WARNING) << "Audio track not ready";
    return -1;
  }

  const uint8_t* data = frame->data();
  size_t size = frame->size();
  auto* audio_info = frame->audio_info();

  if (!data || size == 0) {
    AVE_LOG(LS_WARNING) << "Invalid audio data";
    return -1;
  }

  // Use blocking write with 2ms timeout so we make progress without
  // holding the mutex for an extended period.
  uint32_t frame_count = 0;
  if (audio_info && IsCompressedOutputLocked()) {
    if (audio_info->samples_per_channel > 0) {
      frame_count = static_cast<uint32_t>(audio_info->samples_per_channel);
    } else if (audio_info->duration.IsFinite() &&
               current_audio_config_.sample_rate > 0) {
      const int64_t scaled =
          audio_info->duration.us() * current_audio_config_.sample_rate;
      frame_count = static_cast<uint32_t>((scaled + 500000LL) / 1000000LL);
    }
  }

  ssize_t bytes_written = audio_track_->Write(data, size, true, frame_count);

  AVE_LOG(LS_INFO) << "WriteAudioData: requested=" << size
                   << " written=" << bytes_written
                   << " frame_count=" << static_cast<int64_t>(frame_count);

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

  if (!audio_info->pts.IsFinite()) {
    return;
  }

  int64_t frame_pts_us = audio_info->pts.us();
  int64_t frame_duration_us =
      audio_info->duration.IsFinite() ? audio_info->duration.us() : 0;
  const bool is_compressed = (current_audio_config_.format & 0xFF000000u) != 0;
  int64_t frame_end_pts_us = frame_pts_us + frame_duration_us;

  if (is_compressed && !passthrough_media_start_pts_valid_) {
    passthrough_media_start_pts_us_ = frame_pts_us;
    passthrough_media_start_pts_valid_ = true;
  }

  if (is_compressed) {
    UpdateCompressedSyncAnchorLocked(frame_end_pts_us);
    return;
  }

  int64_t current_sys_time_us = base::TimeMicros();
  int64_t buffer_latency_us =
      audio_track_ ? audio_track_->GetBufferDurationInUs() : 0;

  // Anchor the master clock to what is *currently being heard*, not what was
  // just written.  The original formula (pts, now + latency) was intended to
  // encode "at time now+latency the clock will be pts", but GetMasterClock
  // clamps negative delta to 0, which returns pts immediately — completely
  // defeating the latency compensation and making master_clock == write_pts.
  //
  // Using (pts - latency, now) means: "right now the heard position is
  // pts - latency".  GetMasterClock then advances with real time from that
  // base, so after `latency` seconds the clock correctly reaches `pts`.
  // The monotonic-advancement guard in UpdateAnchor prevents rapid burst
  // writes from jumping the clock backwards between bursts.
  int64_t heard_pts_us = frame_end_pts_us - buffer_latency_us;

  GetAVSyncController()->UpdateAnchor(heard_pts_us, current_sys_time_us,
                                      frame_end_pts_us);
}

void AVPAudioRender::UpdateCompressedSyncAnchorLocked(
    int64_t frame_end_pts_us) {
  if (!GetAVSyncController() || !audio_track_ ||
      current_audio_config_.sample_rate <= 0) {
    return;
  }

  if (frame_end_pts_us > passthrough_last_written_end_pts_us_) {
    passthrough_last_written_end_pts_us_ = frame_end_pts_us;
  } else {
    frame_end_pts_us = passthrough_last_written_end_pts_us_;
  }

  uint32_t played_frames = 0;
  const bool have_position = audio_track_->GetPosition(&played_frames) == OK;
  const int64_t current_sys_time_us = base::TimeMicros();
  if (played_frames == 0 && !passthrough_position_started_) {
    if (last_position_log_time_us_ == 0 ||
        current_sys_time_us - last_position_log_time_us_ >= 500000) {
      AVE_LOG(LS_INFO) << "Offload position: waiting for playout start"
                       << " start_pts_ms="
                       << (passthrough_media_start_pts_valid_
                               ? passthrough_media_start_pts_us_ / 1000
                               : -1)
                       << " played_frames=0";
      last_position_log_time_us_ = current_sys_time_us;
    }
    return;
  }

  if (!have_position) {
    return;
  }

  passthrough_position_started_ = true;
  const int64_t played_duration_us = static_cast<int64_t>(played_frames) *
                                     1000000LL /
                                     current_audio_config_.sample_rate;
  int64_t heard_pts_us =
      passthrough_media_start_pts_valid_
          ? passthrough_media_start_pts_us_ + played_duration_us
          : played_duration_us;
  if (frame_end_pts_us > 0 && heard_pts_us > frame_end_pts_us) {
    heard_pts_us = frame_end_pts_us;
  }

  GetAVSyncController()->UpdateAnchor(heard_pts_us, current_sys_time_us,
                                      frame_end_pts_us);

  if (last_position_log_time_us_ == 0 ||
      current_sys_time_us - last_position_log_time_us_ >= 500000) {
    if (played_frames < last_played_frames_) {
      AVE_LOG(LS_WARNING) << "Offload position: played_frames="
                          << static_cast<int64_t>(played_frames)
                          << " media_pts_ms=" << heard_pts_us / 1000;
    } else {
      AVE_LOG(LS_INFO) << "Offload position: played_frames="
                       << static_cast<int64_t>(played_frames)
                       << " media_pts_ms=" << heard_pts_us / 1000;
    }
    last_position_log_time_us_ = current_sys_time_us;
  }
  last_played_frames_ = played_frames;
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
  // Blocking writes in WriteAudioData provide natural pacing, so no
  // additional delay is needed between frames.
  return 0;
}

}  // namespace player
}  // namespace ave
