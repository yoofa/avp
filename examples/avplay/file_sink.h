/*
 * file_sink.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_EXAMPLES_AVPLAY_FILE_SINK_H_
#define AVP_EXAMPLES_AVPLAY_FILE_SINK_H_

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

#include "base/types.h"
#include "media/audio/audio_device.h"
#include "media/audio/audio_track.h"
#include "media/foundation/media_frame.h"
#include "media/video/video_render.h"

namespace ave {
namespace player {

/**
 * @brief Video render sink that writes raw YUV420P frames to a file.
 *
 * Output file contains concatenated YUV planes with no header.
 * Frame dimensions are printed to stderr on first frame.
 */
class YuvFileVideoRender : public media::VideoRender {
 public:
  /**
   * @param path Path to output .yuv file.
   */
  explicit YuvFileVideoRender(const std::string& path);
  ~YuvFileVideoRender() override;

  void OnFrame(const std::shared_ptr<media::MediaFrame>& frame) override;

  int64_t frames_written() const { return frames_written_.load(); }
  int32_t width() const { return width_; }
  int32_t height() const { return height_; }

 private:
  std::string path_;
  FILE* file_ = nullptr;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int64_t last_pts_us_ = 0;
  std::atomic<int64_t> frames_written_{0};
};

/**
 * @brief Audio track sink that writes PCM audio to a WAV file.
 *
 * Writes a valid WAV file with the correct header updated at Close().
 */
class WavFileAudioTrack : public media::AudioTrack {
 public:
  /**
   * @param path Path to output .wav file.
   */
  explicit WavFileAudioTrack(const std::string& path);
  ~WavFileAudioTrack() override;

  // AudioTrack interface
  bool ready() const override { return ready_; }
  ssize_t bufferSize() const override { return 4096; }
  ssize_t frameCount() const override { return 1024; }
  ssize_t channelCount() const override { return channels_; }
  ssize_t frameSize() const override { return channels_ * bytes_per_sample_; }
  uint32_t sampleRate() const override { return sample_rate_; }
  uint32_t latency() const override { return 0; }
  float msecsPerFrame() const override {
    return sample_rate_ ? (1000.0f / sample_rate_) : 0.0f;
  }
  status_t GetPosition(uint32_t* position) const override;
  int64_t GetPlayedOutDurationUs(int64_t nowUs) const override;
  status_t GetFramesWritten(uint32_t* frameswritten) const override;
  int64_t GetBufferDurationInUs() const override { return 0; }

  status_t Open(media::audio_config_t config,
                AudioCallback cb,
                void* cookie) override;
  ssize_t Write(const void* buffer, size_t size, bool blocking) override;
  status_t Start() override;
  void Stop() override;
  void Flush() override;
  void Pause() override;
  void Close() override;

  uint64_t bytes_written() const { return bytes_written_; }

 private:
  void WriteWavHeader();
  void UpdateWavHeader();

  std::string path_;
  FILE* file_ = nullptr;
  bool ready_ = false;
  bool playing_ = false;
  uint32_t sample_rate_ = 44100;
  int32_t channels_ = 2;
  int32_t bytes_per_sample_ = 2;
  uint64_t bytes_written_ = 0;
  uint32_t frames_written_ = 0;
  std::mutex mutex_;
};

/**
 * @brief Audio device that creates WavFileAudioTrack instances.
 */
class FileAudioDevice : public media::AudioDevice {
 public:
  /**
   * @param path Path prefix for audio output (e.g. "output" → "output.wav").
   */
  explicit FileAudioDevice(const std::string& path);
  ~FileAudioDevice() override = default;

  status_t Init() override { return 0; }

  std::shared_ptr<media::AudioTrack> CreateAudioTrack() override;
  std::shared_ptr<media::AudioRecord> CreateAudioRecord() override {
    return nullptr;
  }
  std::shared_ptr<media::AudioLoopback> CreateAudioLoopback() override {
    return nullptr;
  }
  std::vector<std::pair<int, media::AudioDeviceInfo>> GetSupportedAudioDevices()
      override {
    return {};
  }
  status_t SetAudioInputDevice(int device_id) override { return 0; }
  status_t SetAudioOutputDevice(int device_id) override { return 0; }

 private:
  std::string path_;
};

}  // namespace player
}  // namespace ave

#endif  // AVP_EXAMPLES_AVPLAY_FILE_SINK_H_
