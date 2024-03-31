/*
 * audio_sink.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#include <memory>

#include "audio/audio_format.h"
#include "audio/channel_layout.h"
#include "media/message.h"
#include "player/audio_frame.h"

namespace avp {

#define DEFAULT_AUDIOSINK_BUFFERCOUNT 4
#define DEFAULT_AUDIOSINK_BUFFERSIZE 1200
#define DEFAULT_AUDIOSINK_SAMPLERATE 44100

class AudioSink : public ave::MessageObject {
 public:
  // TODO(youfa) support AudioCallback mode
  // Callback returns the number of bytes actually written to the buffer.
  typedef size_t (*AudioCallback)(AudioSink* audioSink,
                                  void* buffer,
                                  size_t size,
                                  void* cookie);

  AudioSink() = default;
  virtual ~AudioSink() = default;

  virtual bool ready() const = 0;  // audio output is open and ready
  virtual ssize_t channelCount() const = 0;
  virtual uint32_t latency() const = 0;
  virtual status_t getPosition(uint32_t* position) const = 0;
  virtual uint32_t getSampleRate() const = 0;
  virtual int64_t getBufferDurationInUs() const = 0;

  // If no callback is specified, use the "write" API below to submit
  // audio data.
  virtual status_t open(uint32_t sampleRate,
                        int channelCount,
                        ChannelLayout channelLayout,
                        AudioFormat format = AUDIO_FORMAT_PCM_16_BIT,
                        int bufferCount = DEFAULT_AUDIOSINK_BUFFERCOUNT,
                        AudioCallback cb = nullptr) = 0;

  virtual status_t start() = 0;

  /* Input parameter |size| is in byte units stored in |buffer|.
   * Data is copied over and actual number of bytes written (>= 0)
   * is returned, or no data is copied and a negative status code
   * is returned (even when |blocking| is true).
   * When |blocking| is false, AudioSink will immediately return after
   * part of or full |buffer| is copied over.
   * When |blocking| is true, AudioSink will wait to copy the entire
   * buffer, unless an error occurs or the copy operation is
   * prematurely stopped.
   */
  virtual ssize_t write(const void* buffer,
                        size_t size,
                        bool blocking = true) = 0;

  virtual void stop() = 0;
  virtual void flush() = 0;
  virtual void pause() = 0;
  virtual void close() = 0;

  virtual status_t setPlaybackRate(float rate) = 0;
  virtual status_t getPlaybackRate(float* rate) = 0;

  virtual void onFrame(std::shared_ptr<Buffer>& frame) = 0;
};
} /* namespace avp */

#endif /* !AUDIO_SINK_H */
