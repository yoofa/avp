/*
 * avp_audio_render.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_AVP_AVP_AUDIO_RENDER_H_H_
#define AVE_AVP_AVP_AUDIO_RENDER_H_H_

#include <memory>
#include <mutex>

#include "avp_render.h"
#include "media/audio/audio_device.h"
#include "media/audio/audio_track.h"

namespace ave {
namespace player {

/**
 * @brief Audio renderer implementation that supports both PCM and offload
 * formats. Provides audio sink management, format change detection, and
 * playback rate control.
 */
class AVPAudioRender : public AVPRender {
 public:
  /**
   * @brief Constructs AVPAudioRender with required parameters.
   * @param task_runner_factory Factory for creating task runners.
   * @param avsync_controller Pointer to the AV sync controller.
   * @param audio_device Shared pointer to the audio device for creating audio
   * tracks.
   * @param master_stream Whether this is the master audio stream that updates
   * the clock.
   */
  AVPAudioRender(base::TaskRunnerFactory* task_runner_factory,
                 IAVSyncController* avsync_controller,
                 std::shared_ptr<media::AudioDevice> audio_device,
                 bool master_stream = true);

  ~AVPAudioRender() override;

  /**
   * @brief Opens the audio sink with specified configuration.
   *        This should be called during player prepare phase before receiving
   * data.
   * @param config The audio configuration for the sink.
   * @return Status of the operation.
   */
  status_t OpenAudioSink(const media::audio_config_t& config);

  /**
   * @brief Closes the audio sink and releases resources.
   */
  void CloseAudioSink();

  /**
   * @brief Checks if the audio sink is ready for playback.
   * @return True if ready, false otherwise.
   */
  bool IsAudioSinkReady() const;

  /**
   * @brief Sets the playback rate for audio playback.
   * @param rate The playback rate (1.0 = normal speed).
   */
  void SetPlaybackRate(float rate);

  /**
   * @brief Gets the current playback rate.
   * @return The current playback rate.
   */
  float GetPlaybackRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playback_rate_;
  }

  /**
   * @brief Starts the audio renderer.
   */
  void Start() override EXCLUDES(mutex_);

  /**
   * @brief Stops the audio renderer.
   */
  void Stop() override EXCLUDES(mutex_);

  /**
   * @brief Pauses the audio renderer.
   */
  void Pause() override EXCLUDES(mutex_);

  /**
   * @brief Resumes the audio renderer.
   */
  void Resume() override EXCLUDES(mutex_);

  /**
   * @brief Flushes all pending audio frames.
   */
  void Flush() override EXCLUDES(mutex_);

 protected:
  /**
   * @brief Internal frame rendering method that handles audio data.
   * @param frame The audio frame to render.
   * @param render Whether to actually render the frame.
   * @return Next frame delay in microseconds.
   */
  uint64_t RenderFrameInternal(
      std::shared_ptr<media::MediaFrame>& frame) override REQUIRES(mutex_);

 private:
  /**
   * @brief Creates a new audio track with the current configuration.
   * @return Status of the operation.
   */
  status_t CreateAudioTrack() REQUIRES(mutex_);

  /**
   * @brief Destroys the current audio track.
   */
  void DestroyAudioTrack() REQUIRES(mutex_);

  /**
   * @brief Checks if the audio format has changed and needs track recreation.
   * @param frame The audio frame to check.
   * @return True if format changed, false otherwise.
   */
  bool HasAudioFormatChanged(const std::shared_ptr<media::MediaFrame>& frame)
      REQUIRES(mutex_);

  /**
   * @brief Converts MediaFrame audio info to audio_config_t.
   * @param frame The audio frame.
   * @return The audio configuration.
   */
  media::audio_config_t ConvertToAudioConfig(
      const std::shared_ptr<media::MediaFrame>& frame) REQUIRES(mutex_);

  /**
   * @brief Writes audio data to the audio track.
   * @param frame The audio frame containing data to write.
   * @return Number of bytes written or negative error code.
   */
  ssize_t WriteAudioData(const std::shared_ptr<media::MediaFrame>& frame)
      REQUIRES(mutex_);

  /**
   * @brief Updates the sync controller anchor if this is the master stream.
   * @param frame The audio frame being rendered.
   */
  void UpdateSyncAnchor(const std::shared_ptr<media::MediaFrame>& frame)
      REQUIRES(mutex_);

  /**
   * @brief Checks if the audio track supports playback rate changes.
   * @return True if supported, false otherwise.
   */
  bool SupportsPlaybackRateChange() const REQUIRES(mutex_);

  /**
   * @brief Applies playback rate to the audio track if supported.
   */
  void ApplyPlaybackRate() REQUIRES(mutex_);

  /**
   * @brief Calculates the delay for the next audio frame based on real playback
   * latency.
   * @return Delay in microseconds for the next audio frame.
   */
  int64_t CalculateNextAudioFrameDelay() REQUIRES(mutex_);

  std::shared_ptr<media::AudioDevice> audio_device_ GUARDED_BY(mutex_);
  std::shared_ptr<media::AudioTrack> audio_track_ GUARDED_BY(mutex_);
  bool master_stream_ GUARDED_BY(mutex_);
  bool audio_sink_ready_ GUARDED_BY(mutex_);
  float playback_rate_ GUARDED_BY(mutex_);

  // Audio format tracking
  media::audio_config_t current_audio_config_ GUARDED_BY(mutex_);
  bool format_initialized_ GUARDED_BY(mutex_);

  // Audio track capabilities
  bool supports_playback_rate_ GUARDED_BY(mutex_);
  bool supports_timestamp_ GUARDED_BY(mutex_);

  // Statistics
  int64_t total_bytes_written_ GUARDED_BY(mutex_);
  int64_t last_audio_pts_us_ GUARDED_BY(mutex_);
};

}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_AVP_AUDIO_RENDER_H_H_ */
