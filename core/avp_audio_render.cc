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
    if (IsRunningLocked() && !IsPausedLocked() && audio_track_) {
      status_t start_result = audio_track_->Start();
      if (start_result == OK) {
        AVE_LOG(LS_INFO) << "Audio sink opened while renderer already running, "
                            "started track";
      } else {
        AVE_LOG(LS_ERROR)
            << "Audio sink opened but failed to start track, error: "
            << start_result;
        audio_sink_ready_ = false;
        return start_result;
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

    // Reset passthrough pacing state on every fresh start so the new session
    // measures ahead-ness relative to its own start time.
    passthrough_start_time_us_ = 0;
    passthrough_start_pts_us_ = 0;
    passthrough_media_start_pts_us_ = 0;
    passthrough_media_start_pts_valid_ = false;
    passthrough_position_started_ = false;
    last_position_log_time_us_ = 0;
    last_played_frames_ = 0;

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

    // Reset passthrough pacing so the next burst of frames after a seek/flush
    // is measured from the new position, not the old one.
    passthrough_start_time_us_ = 0;
    passthrough_start_pts_us_ = 0;
    passthrough_media_start_pts_us_ = 0;
    passthrough_media_start_pts_valid_ = false;
    passthrough_position_started_ = false;
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
    // Renderer is already running (we are inside RenderFrameInternal),
    // so start the track immediately.
    AVE_LOG(LS_INFO)
        << "RenderFrameInternal[AUDIO]: starting track after lazy init";
    audio_track_->Start();
    format_initialized_ = true;
    audio_sink_ready_ = true;
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
    // Start the newly created track — we are already inside the render loop.
    audio_track_->Start();
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

  // Write audio data to track.  Single-shot write with a short timeout so
  // the caller's mutex is not held for a long time.  Partial writes are
  // handled via cached_frame_ and a 0-delay re-schedule.
  ssize_t bytes_written = WriteAudioData(frame_to_write);
  if (bytes_written > 0) {
    total_bytes_written_ += bytes_written;
    const bool full_frame_written =
        static_cast<size_t>(bytes_written) >= requested_size;

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

  // Passthrough pacing: for compressed audio formats, AudioTrack.write()
  // accepts data into a large hardware FIFO without blocking, so all frames
  // can be fed at CPU speed.  This causes the sync anchor PTS to race far
  // ahead of real time (e.g. 2500 ms of audio written in 6 ms wall time),
  // making every video frame appear "late" and be dropped — freezing video.
  //
  // Mitigation: after each passthrough write, check how far ahead of real
  // time we are and return a scheduling delay so the render task is woken
  // only when the next frame is due.  We allow kMaxAheadUs (200 ms) of
  // look-ahead to keep the hardware FIFO topped up without runaway.
  if (is_compressed && !cached_frame_ && written_audio_info &&
      written_audio_info->pts.IsFinite()) {
    int64_t frame_pts_us = written_audio_info->pts.us();
    int64_t frame_duration_us = written_audio_info->duration.IsFinite()
                                    ? written_audio_info->duration.us()
                                    : 0;
    int64_t frame_end_pts_us = frame_pts_us + frame_duration_us;
    int64_t now_us = base::TimeMicros();

    if (passthrough_start_time_us_ == 0) {
      passthrough_start_time_us_ = now_us;
      passthrough_start_pts_us_ = frame_end_pts_us;
    }

    // How far ahead of "expected real-time" are we?
    int64_t expected_pts_us =
        passthrough_start_pts_us_ + (now_us - passthrough_start_time_us_);
    int64_t ahead_us = frame_end_pts_us - expected_pts_us;

    constexpr int64_t kMaxAheadUs = 200000;  // 200 ms look-ahead budget
    if (ahead_us > kMaxAheadUs) {
      int64_t delay_us = ahead_us - kMaxAheadUs;
      AVE_LOG(LS_VERBOSE) << "Passthrough pacing: ahead=" << ahead_us / 1000
                          << "ms, delaying " << delay_us / 1000 << "ms";
      return static_cast<uint64_t>(delay_us);
    }
  }

  return 0;
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

  if (!data || size == 0) {
    AVE_LOG(LS_WARNING) << "Invalid audio data";
    return -1;
  }

  // Use blocking write with 2ms timeout so we make progress without
  // holding the mutex for an extended period.
  ssize_t bytes_written = audio_track_->Write(data, size, true);

  AVE_LOG(LS_INFO) << "WriteAudioData: requested=" << size
                   << " written=" << bytes_written;

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
  int64_t current_sys_time_us = base::TimeMicros();
  int64_t buffer_latency_us =
      audio_track_ ? audio_track_->GetBufferDurationInUs() : 0;
  const bool is_compressed = (current_audio_config_.format & 0xFF000000u) != 0;
  int64_t frame_end_pts_us = frame_pts_us + frame_duration_us;
  uint32_t played_frames = 0;
  uint32_t frames_written = 0;
  const bool have_position = is_compressed && audio_track_ &&
                             audio_track_->GetPosition(&played_frames) == OK;
  const bool have_written =
      is_compressed && audio_track_ &&
      audio_track_->GetFramesWritten(&frames_written) == OK;

  if (is_compressed && !passthrough_media_start_pts_valid_) {
    passthrough_media_start_pts_us_ = frame_pts_us;
    passthrough_media_start_pts_valid_ = true;
  }

  if (is_compressed && have_position && current_audio_config_.sample_rate > 0) {
    if (played_frames == 0 && !passthrough_position_started_) {
      if (last_position_log_time_us_ == 0 ||
          current_sys_time_us - last_position_log_time_us_ >= 500000) {
        AVE_LOG(LS_INFO) << "Offload position: waiting for playout start"
                         << " frames_written="
                         << (have_written ? static_cast<int64_t>(frames_written)
                                          : -1)
                         << " start_pts_ms="
                         << (passthrough_media_start_pts_valid_
                                 ? passthrough_media_start_pts_us_ / 1000
                                 : -1)
                         << " played_frames=0";
        last_position_log_time_us_ = current_sys_time_us;
      }
      return;
    }

    passthrough_position_started_ = true;
    const int64_t played_duration_us = static_cast<int64_t>(played_frames) *
                                       1000000LL /
                                       current_audio_config_.sample_rate;
    int64_t heard_pts_us =
        passthrough_media_start_pts_valid_
            ? passthrough_media_start_pts_us_ + played_duration_us
            : frame_pts_us + played_duration_us;
    if (heard_pts_us > frame_end_pts_us) {
      heard_pts_us = frame_end_pts_us;
    }

    GetAVSyncController()->UpdateAnchor(heard_pts_us, current_sys_time_us,
                                        frame_end_pts_us);

    if (last_position_log_time_us_ == 0 ||
        current_sys_time_us - last_position_log_time_us_ >= 500000) {
      if (played_frames < last_played_frames_) {
        AVE_LOG(LS_WARNING)
            << "Offload position: played_frames="
            << static_cast<int64_t>(played_frames) << " frames_written="
            << (have_written ? static_cast<int64_t>(frames_written) : -1)
            << " media_pts_ms=" << heard_pts_us / 1000;
      } else {
        AVE_LOG(LS_INFO) << "Offload position: played_frames="
                         << static_cast<int64_t>(played_frames)
                         << " frames_written="
                         << (have_written ? static_cast<int64_t>(frames_written)
                                          : -1)
                         << " media_pts_ms=" << heard_pts_us / 1000;
      }
      last_position_log_time_us_ = current_sys_time_us;
    }
    last_played_frames_ = played_frames;

    AVE_LOG(LS_INFO) << "UpdateSyncAnchor PTS=" << frame_pts_us / 1000
                     << "ms sys=" << current_sys_time_us / 1000
                     << "ms heard_pts=" << heard_pts_us / 1000
                     << "ms played_frames=" << played_frames;
    return;
  }

  if (have_position && have_written && frames_written > played_frames &&
      current_audio_config_.sample_rate > 0) {
    const int64_t queued_frames =
        static_cast<int64_t>(frames_written) - played_frames;
    buffer_latency_us =
        queued_frames * 1000000LL / current_audio_config_.sample_rate;
  }

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

  if (is_compressed && audio_track_ &&
      (last_position_log_time_us_ == 0 ||
       current_sys_time_us - last_position_log_time_us_ >= 500000)) {
    if (have_position || have_written) {
      if (have_position && played_frames < last_played_frames_) {
        AVE_LOG(LS_WARNING)
            << "Offload position: played_frames="
            << (have_position ? static_cast<int64_t>(played_frames) : -1)
            << " frames_written="
            << (have_written ? static_cast<int64_t>(frames_written) : -1)
            << " buffer_latency_ms=" << buffer_latency_us / 1000;
      } else {
        AVE_LOG(LS_INFO) << "Offload position: played_frames="
                         << (have_position ? static_cast<int64_t>(played_frames)
                                           : -1)
                         << " frames_written="
                         << (have_written ? static_cast<int64_t>(frames_written)
                                          : -1)
                         << " buffer_latency_ms=" << buffer_latency_us / 1000;
      }
      if (have_position) {
        last_played_frames_ = played_frames;
      }
      last_position_log_time_us_ = current_sys_time_us;
    }
  }

  AVE_LOG(LS_INFO) << "UpdateSyncAnchor PTS=" << frame_pts_us / 1000
                   << "ms sys=" << current_sys_time_us / 1000
                   << "ms buf_latency=" << buffer_latency_us / 1000 << "ms";
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
