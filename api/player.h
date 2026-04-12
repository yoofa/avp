/*
 * player.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PLAYER_H
#define PLAYER_H

#include <cstddef>
#include <memory>

#include "base/data_source/data_source.h"
#include "base/errors.h"
#include "media/audio/audio_device.h"
#include "media/codec/codec_factory.h"
#include "media/foundation/media_meta.h"
#include "media/video/video_render.h"

#include "api/content_source/content_source.h"
#include "api/content_source/content_source_factory.h"
#include "api/demuxer/demuxer_factory.h"
#include "api/player_interface.h"

namespace ave {
namespace net {
class HTTPProvider;
}  // namespace net
}  // namespace ave

namespace ave {
namespace player {

/**
 * @brief Policy for audio passthrough (compressed audio direct output).
 *
 * ALWAYS_PCM:          Always decode to PCM (default, current behavior).
 * PREFER_PASSTHROUGH:  Try passthrough if device supports it, fall back to PCM.
 * AUTO:                Let the player decide based on device capabilities.
 */
enum class AudioPassthroughPolicy : uint8_t {
  ALWAYS_PCM = 0,
  PREFER_PASSTHROUGH = 1,
  AUTO = 2,
};

/**
 * @brief The Player class represents a media player.
 */
class Player {
 public:
  /**
   * @brief The Builder class is used to construct a Player object.
   */
  class Builder {
   public:
    /**
     * @brief Constructs a Builder object.
     */
    Builder() = default;

    /**
     * @brief Destroys the Builder object.
     */
    ~Builder() = default;

    /**
     * @brief Sets the content source factory for the player.
     * @param content_source_factory The content source factory to set.
     */
    Builder& setContentSourceFactory(
        std::shared_ptr<ContentSourceFactory> content_source_factory) {
      content_source_factory_ = std::move(content_source_factory);
      return *this;
    }

    /**
     * @brief Sets the demuxer factory for the player.
     * @param demuxer_factory The demuxer factory to set.
     */
    Builder& setDemuxerFactory(
        std::shared_ptr<DemuxerFactory> demuxer_factory) {
      demuxer_factory_ = std::move(demuxer_factory);
      return *this;
    }

    /**
     * @brief Sets the codec factory for the player.
     * @param codec_factory The codec factory to set.
     * @return A reference to the Builder object.
     */
    Builder& setCodecFactory(
        std::shared_ptr<ave::media::CodecFactory> codec_factory) {
      codec_factory_ = std::move(codec_factory);
      return *this;
    }

    /**
     * @brief Sets the audio device for the player.
     * @param audio_device The audio device to use.
     * @return A reference to the Builder object.
     */
    Builder& setAudioDevice(
        std::shared_ptr<ave::media::AudioDevice> audio_device) {
      audio_device_ = std::move(audio_device);
      return *this;
    }

    Builder& setAudioDeviceFactory(
        std::shared_ptr<ave::media::AudioDevice> audio_device) {
      return setAudioDevice(std::move(audio_device));
    }

    /**
     * @brief Sets whether A/V synchronization is enabled.
     * @param enabled True to enable sync-based pacing, false to render
     *        immediately.
     * @return A reference to the Builder object.
     */
    Builder& setSyncEnabled(bool enabled) {
      sync_enabled_ = enabled;
      return *this;
    }

    /**
     * @brief Sets the audio passthrough policy.
     * @param policy The passthrough policy to use for the player lifetime.
     * @return A reference to the Builder object.
     */
    Builder& setAudioPassthroughPolicy(AudioPassthroughPolicy policy) {
      passthrough_policy_ = policy;
      return *this;
    }

    /**
     * @brief Sets whether the player should run in audio-only mode.
     * @param audio_only True to skip the video path, false for normal A/V.
     * @return A reference to the Builder object.
     */
    Builder& setAudioOnly(bool audio_only) {
      audio_only_ = audio_only;
      return *this;
    }

    Builder& setHttpProvider(
        std::shared_ptr<ave::net::HTTPProvider> http_provider) {
      http_provider_ = std::move(http_provider);
      return *this;
    }

    /**
     * @brief Builds and returns a shared pointer to a Player object.
     * @return A shared pointer to a Player object.
     */
    std::shared_ptr<Player> build();

   private:
    std::shared_ptr<ContentSourceFactory> content_source_factory_;
    std::shared_ptr<DemuxerFactory> demuxer_factory_;
    std::shared_ptr<ave::media::CodecFactory> codec_factory_;
    std::shared_ptr<ave::media::AudioDevice> audio_device_;
    std::shared_ptr<ave::net::HTTPProvider> http_provider_;
    bool sync_enabled_ = true;
    AudioPassthroughPolicy passthrough_policy_ =
        AudioPassthroughPolicy::ALWAYS_PCM;
    bool audio_only_ = false;
  };

  /**
   * @brief The Listener class is the base class for player event listeners.
   */
  class Listener {
   protected:
    /**
     * @brief Constructs a Listener object.
     */
    Listener() = default;

    /**
     * @brief Destroys the Listener object.
     */
    virtual ~Listener() = default;

   public:
    /**
     * @brief Called when the player is prepared and ready for playback.
     * @param err OK on success, or an error code on failure.
     */
    virtual void OnPrepared(status_t err) {}

    /**
     * @brief Called when playback reaches the end of stream.
     */
    virtual void OnCompletion() {}

    /**
     * @brief Called when an error occurs during playback.
     * @param error The error code.
     */
    virtual void OnError(status_t error) {}

    /**
     * @brief Called when a seek operation is completed.
     */
    virtual void OnSeekComplete() {}

    /**
     * @brief Called to report buffering progress.
     * @param percent The buffering progress in percentage (0-100).
     */
    virtual void OnBufferingUpdate(int percent) {}

    /**
     * @brief Called when the video size changes.
     * @param width The new video width in pixels.
     * @param height The new video height in pixels.
     */
    virtual void OnVideoSizeChanged(int width, int height) {}

    /**
     * @brief Called to report informational events.
     * @param what The info type code.
     * @param extra Extra info code, specific to the info type.
     */
    virtual void OnInfo(int what, int extra) {}
  };

  /**
   * @brief Constructs a Player object.
   */
  Player() = default;

  /**
   * @brief Destroys the Player object.
   */
  virtual ~Player() = default;

  /**
   * @brief Initializes the player.
   * @return The status of the initialization.
   */
  virtual status_t Init() = 0;

  /**
   * @brief Sets the listener for player events.
   * @param listener The listener to set.
   * @return The status of the operation.
   */
  virtual status_t SetListener(std::shared_ptr<Listener> listener) = 0;

  /**
   * @brief Sets the data source for the player using a URL.
   * @param url The URL of the data source.
   * @param headers The headers to use for the http connection.
   * @return The status of the operation.
   */
  virtual status_t SetDataSource(
      const char* url,
      const std::unordered_map<std::string, std::string>& headers) = 0;

  /**
   * @brief Sets the data source for the player using a file descriptor.
   * @param fd The file descriptor of the data source.
   * @param offset The offset within the file.
   * @param length The length of the data.
   * @return The status of the operation.
   */
  virtual status_t SetDataSource(int fd, int64_t offset, int64_t length) = 0;

  /**
   * @brief Sets the data source for the player using a custom data source
   * object.
   * @param source The custom data source object.
   * @return The status of the operation.
   */
  virtual status_t SetDataSource(std::shared_ptr<ave::DataSource> source) = 0;

  /**
   * @brief Sets the data source for the player using a custom content source
   * object.
   * @param source The custom content source object.
   * @return The status of the operation.
   */
  virtual status_t SetDataSource(std::shared_ptr<ContentSource> source) = 0;

  /**
   * @brief Sets the video render for the player.
   * @param video_render The video render to set.
   * @return The status of the operation.
   */
  virtual status_t SetVideoRender(
      std::shared_ptr<ave::media::VideoRender> video_render) = 0;

  /**
   * @brief Prepares the player for playback.
   * @return The status of the preparation.
   */
  virtual status_t Prepare() = 0;

  /**
   * @brief Starts playback.
   * @return The status of the operation.
   */
  virtual status_t Start() = 0;

  /**
   * @brief Stops playback.
   * @return The status of the operation.
   */
  virtual status_t Stop() = 0;

  /**
   * @brief Synchronous stop: blocks the calling thread until the player has
   *        fully stopped (renders halted, decoders shut down). Must NOT be
   *        called from the player's internal looper thread.
   * @return The status of the operation.
   */
  virtual status_t StopSync() = 0;

  /**
   * @brief Prepares the player for destruction by stopping and joining the
   *        internal looper thread from the caller's thread. Must be called
   *        before the last shared_ptr to this player is released to prevent
   *        a self-join deadlock when ~Player() is triggered from the looper.
   *        Must NOT be called from the player's internal looper thread.
   */
  virtual void PrepareDestroy() = 0;

  /**
   * @return The status of the operation.
   */
  virtual status_t Pause() = 0;

  /**
   * @brief Resumes playback.
   * @return The status of the operation.
   */
  virtual status_t Resume() = 0;

  /**
   * @brief Seeks to a specified position in the media.
   * @param msec The position to seek to, in milliseconds.
   * @param mode The seek mode.
   * @return The status of the operation.
   */
  virtual status_t SeekTo(int msec, SeekMode mode) = 0;

  /**
   * @brief Seeks to a specified position in the media using the default seek
   * mode.
   * @param msec The position to seek to, in milliseconds.
   * @return The status of the operation.
   */
  status_t SeekTo(int msec) {
    return SeekTo(msec, SeekMode::SEEK_PREVIOUS_SYNC);
  }

  /**
   * @brief Resets the player to its uninitialized state.
   * @return The status of the operation.
   */
  virtual status_t Reset() = 0;

  /**
   * @brief Gets the duration of the media content.
   * @param msec Output parameter for duration in milliseconds.
   * @return OK on success, or an error code.
   */
  virtual status_t GetDuration(int* msec) = 0;

  /**
   * @brief Gets the current playback position.
   * @param msec Output parameter for position in milliseconds.
   * @return OK on success, or an error code.
   */
  virtual status_t GetCurrentPosition(int* msec) = 0;

  /**
   * @brief Returns whether the player is currently playing.
   * @return True if playing, false otherwise.
   */
  virtual bool IsPlaying() const = 0;

  /**
   * @brief Gets the video width in pixels.
   * @return The video width, or 0 if not available.
   */
  virtual int GetVideoWidth() const = 0;

  /**
   * @brief Gets the video height in pixels.
   * @return The video height, or 0 if not available.
   */
  virtual int GetVideoHeight() const = 0;

  /**
   * @brief Sets the playback rate.
   * @param rate The playback rate (1.0 = normal speed).
   * @return OK on success, or an error code.
   */
  virtual status_t SetPlaybackRate(float rate) = 0;

  /**
   * @brief Gets the current playback rate.
   * @return The current playback rate.
   */
  virtual float GetPlaybackRate() const = 0;

  /**
   * @brief Sets the volume for left and right channels.
   * @param left_volume Left channel volume (0.0 to 1.0).
   * @param right_volume Right channel volume (0.0 to 1.0).
   * @return OK on success, or an error code.
   */
  virtual status_t SetVolume(float left_volume, float right_volume) = 0;

  /**
   * @brief Gets the number of tracks in the media.
   * @return The number of tracks.
   */
  virtual size_t GetTrackCount() const = 0;

  /**
   * @brief Gets the media metadata for the specified track.
   * @param index The track index.
   * @return Shared pointer to MediaMeta, or nullptr if not found.
   */
  virtual std::shared_ptr<ave::media::MediaMeta> GetTrackInfo(
      size_t index) const = 0;

  /**
   * @brief Selects or deselects the specified track.
   * @param index The track index.
   * @param select True to select, false to deselect.
   * @return OK on success, or an error code.
   */
  virtual status_t SelectTrack(size_t index, bool select) = 0;

 protected:
  /**
   * @brief Returns a weak pointer to the listener.
   * @return A weak pointer to the listener.
   */
  // std::weak_ptr<Listener>& listener() { return listener_; }

 private:
  // std::weak_ptr<Listener> listener_;
};

}  // namespace player
}  // namespace ave

#endif /* !PLAYER_H */
