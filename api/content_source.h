/*
 * content_source.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef CONTENT_SOURCE_H
#define CONTENT_SOURCE_H

#include <memory>

#include "base/errors.h"

#include "media/foundation/media_format.h"
#include "media/foundation/media_packet.h"

#include "player_interface.h"

namespace avp {

using ave::media::MediaFormat;
using ave::media::MediaType;

class ContentSource : public ave::media::MessageObject {
 public:
  enum Flags : int32_t {
    FLAG_CAN_PAUSE = 1,
    FLAG_CAN_SEEK_BACKWARD = 2,  // the "10 sec back button"
    FLAG_CAN_SEEK_FORWARD = 4,   // the "10 sec forward button"
    FLAG_CAN_SEEK = 8,           // the "seek bar"
    FLAG_DYNAMIC_DURATION = 16,
    FLAG_SECURE = 32,  // Secure codec is required.
    FLAG_PROTECTED =
        64,  // The screen needs to be protected (screenshot is disabled).
  };

  class Notify {
   public:
    Notify() = default;
    virtual ~Notify() = default;

    /**
     * @brief Called when the content source is prepared and ready to start
     * playback.
     */
    virtual void OnPrepared(status_t err) = 0;

    /**
     *
     */
    virtual void OnFlagsChanged(int32_t flags) = 0;

    /**
     *
     */
    virtual void OnVideoSizeChanged(std::shared_ptr<MediaFormat>& format) = 0;

    /**
     * @brief Called when a seek operation is completed.
     */
    virtual void OnSeekComplete() {}

    /**
     * @brief Called when buffering of the content starts.
     */
    virtual void OnBufferingStart() {}

    /**
     * @brief Called to update the buffering progress.
     * @param percent The buffering progress in percentage.
     */
    virtual void OnBufferingUpdate(int percent) {}

    /**
     * @brief Called when buffering of the content ends.
     */
    virtual void OnBufferingEnd() {}

    /**
     * @brief Called when the playback of the content is completed.
     */
    virtual void OnCompletion() = 0;

    /**
     * @brief Called when an error occurs during playback.
     * @param error The error code.
     */
    virtual void OnError(status_t error) = 0;

    /**
     * @brief Called when data needs to be fetched for a specific track.
     * @param stream_type The type of the track for which data needs to be
     * fetched.
     */
    virtual void OnFetchData(MediaType stream_type) = 0;
  };

  ContentSource() = default;
  ~ContentSource() override = default;

  /**
   * @brief Sets the notify object for the content source.
   *
   * This function sets the notify object for the content source. The notify
   * object is responsible for receiving notifications from the content source.
   *
   * @param notify A shared pointer to the Notify object.
   */
  virtual void SetNotify(std::shared_ptr<Notify> notify) = 0;

  /**
   * @brief Prepare the content source for playback.
   */
  virtual void Prepare() = 0;

  /**
   * @brief Start playback.
   */
  virtual void Start() = 0;

  /**
   * @brief Stop playback.
   */
  virtual void Stop() = 0;

  /**
   * @brief Pause playback.
   */
  virtual void Pause() = 0;

  /**
   * @brief Resume playback.
   */
  virtual void Resume() = 0;

  /**
   * @brief Dequeues an access unit from the specified track.
   * @param track_type The media type of the track.
   * @param access_unit The output parameter to store the dequeued access unit.
   * @return The status of the operation.
   */
  virtual status_t DequeueAccessUnit(
      MediaType track_type,
      std::shared_ptr<ave::media::MediaPacket>& access_unit) = 0;

  /**
   * @brief Retrieves the media format associated with the content source.
   *
   * This function returns a shared pointer to the `MediaFormat` object that
   * represents the format of the media content provided by the content source.
   *
   * @return A shared pointer to the `MediaFormat` object representing the media
   * format.
   */
  virtual std::shared_ptr<MediaFormat> GetFormat() = 0;

  /**
   * @brief Gets the duration of the content source.
   * @param duration_us The output parameter to store the duration in
   * microseconds.
   * @return The status of the operation.
   */
  virtual status_t GetDuration(int64_t* /* duration_us */) {
    return ave::INVALID_OPERATION;
  }

  /**
   * @brief Gets the number of tracks in the content source.
   * @return The number of tracks.
   */
  virtual size_t GetTrackCount() const { return 0; }

  /**
   * @brief Gets the media format of the specified track.
   * @param track_index The index of the track.
   * @return The media format of the track.
   */
  virtual std::shared_ptr<MediaFormat> GetTrackInfo(
      size_t /* track_index */) const {
    return {};
  }

  /**
   * @brief Selects or deselects the specified track.
   * @param track_index The index of the track.
   * @param select True to select the track, false to deselect.
   * @return The status of the operation.
   */
  virtual status_t SelectTrack(size_t /* track_index */, bool /* select */
  ) {
    return ave::INVALID_OPERATION;
  }

  /**
   * @brief Seeks to the specified time position.
   * @param seek_time_us The time position to seek to in microseconds.
   * @param mode The seek mode.
   * @return The status of the operation.
   */
  virtual status_t SeekTo(int64_t /* seek_time_us */, SeekMode /* mode */) {
    return ave::INVALID_OPERATION;
  }
};

}  // namespace avp

#endif /* !CONTENT_SOURCE_H */
