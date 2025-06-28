/*
 * player.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PLAYER_H
#define PLAYER_H

#include <memory>

#include "base/data_source/data_source.h"
#include "base/errors.h"
#include "media/audio/audio_device.h"
#include "media/codec/codec_factory.h"
#include "media/video/video_render.h"

#include "api/content_source/content_source.h"
#include "api/content_source/content_source_factory.h"
#include "api/demuxer/demuxer_factory.h"
#include "api/player_interface.h"

namespace ave {
namespace player {

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
     * @brief Sets the audio device factory for the player.
     * @param audio_device_factory The audio device factory to set.
     * @return A reference to the Builder object.
     */
    Builder& setAudioDeviceFactory(
        std::shared_ptr<ave::media::AudioDevice> audio_device_module) {
      audio_device_module_ = std::move(audio_device_module);
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
    std::shared_ptr<ave::media::AudioDevice> audio_device_module_;
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
    virtual void OnCompletion() {}
    virtual void OnError(status_t error) {}
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
   * @brief Pauses playback.
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
