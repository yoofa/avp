/*
 * avp_render_synchronizer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_RENDER_SYNCHRONIZER_H
#define AVP_RENDER_SYNCHRONIZER_H

#include "common/handler.h"
#include "common/looper.h"
#include "common/message.h"
#include "player/audio_sink.h"
#include "player/media_clock.h"
#include "player/video_sink.h"

namespace avp {
class AvpRenderSynchronizer : public Handler, public MessageObject {
 public:
  AvpRenderSynchronizer(std::shared_ptr<Message> msg,
                        std::shared_ptr<Looper> looper,
                        std::shared_ptr<MediaClock> mediaClock);
  virtual ~AvpRenderSynchronizer();
  void init();
  void setAudioSink(const std::shared_ptr<AudioSink> audioSink);
  void setVideoSink(const std::shared_ptr<VideoSink> videoSink);
  void queueBuffer(bool audio,
                   const std::shared_ptr<Buffer> buffer,
                   const std::shared_ptr<Message> renderMessage);

  void pause();
  void resume();

  void flush();

 private:
  enum {
    kWhatSetAudioSink = 'setA',
    kWhatSetVideoSink = 'setV',
    kWhatQueueBuffer = 'qbuf',
    kWhatRenderAudio = 'rndA',
    kWhatRenderVideo = 'rndV',

    kWhatPause = 'paus',
    kWhatResume = 'resu',
  };

  struct QueueEntry {
    std::shared_ptr<Buffer> mBuffer;
    std::shared_ptr<Message> mConsumedNotify;
  };

  int64_t getRealTimeUs(int64_t mediaTimeus, int64_t nowUs);

  void onSetAudioSink(const std::shared_ptr<AudioSink>& audioSink);
  void onSetVideoSink(const std::shared_ptr<VideoSink>& videoSink);
  void onQueueBuffer(const std::shared_ptr<Message>& msg);
  void onPause();
  void onResume();

  void postDrainAudio();
  void onRenderAudio(const std::shared_ptr<AudioFrame>& audioFrame);
  void postDrainVideo();
  void onRenderVideo(const std::shared_ptr<VideoFrame>& videoFrame);

  void onMessageReceived(const std::shared_ptr<Message>& message) override;

  std::shared_ptr<Message> mNotify;
  std::shared_ptr<Looper> mLooper;
  std::shared_ptr<MediaClock> mMediaClock;

  float mPlaybackRate;

  std::shared_ptr<AudioSink> mAudioSink;
  std::shared_ptr<VideoSink> mVideoSink;
  std::queue<QueueEntry> mAudioQueue;
  std::queue<QueueEntry> mVideoQueue;

  bool mDrainAudioQueuePending;
  bool mDrainVideoQueuePending;
  int32_t mAudioQueueGeneration;
  int32_t mVideoQueueGeneration;
  int32_t mAudioDrainGeneration;
  int32_t mVideoDrainGeneration;
  int32_t mAudioEOSGeneration;

  int64_t mAudioFirstAnchorTimeMediaUs;
  int64_t mAnchorTimeMediaUs;
  int64_t mAnchorNumFramesWritten;
  int64_t mVideoLateByUs;
  int64_t mNextVideoTimeMediaUs;
  bool mHasAudio;
  bool mHasVideo;
  bool mUseAudioCallback;

  bool mPaused;
  bool mFirstVideoFrameReceived;
};
} /* namespace avp */

#endif /* !AVP_RENDER_SYNCHRONIZER_H */
