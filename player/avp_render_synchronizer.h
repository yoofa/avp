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
#include "player/video_sink.h"

namespace avp {
class AvpRenderSynchronizer : public Handler, public MessageObject {
 public:
  AvpRenderSynchronizer(std::shared_ptr<Message> msg,
                        std::shared_ptr<Looper> looper);
  virtual ~AvpRenderSynchronizer();
  void init();
  void setAudioSink(const std::shared_ptr<AudioSink> audioSink);
  void setVideoSink(const std::shared_ptr<VideoSink> videoSink);
  void queueBuffer(bool audio,
                   const std::shared_ptr<Buffer> buffer,
                   const std::shared_ptr<Message> renderMessage);

 private:
  enum {
    kWhatSetAudioSink = 'setA',
    kWhatSetVideoSink = 'setV',
    kWhatQueueBuffer = 'qbuf',
    kWhatRenderAudio = 'rndA',
    kWhatRenderVideo = 'rndV',
  };

  void onSetAudioSink(const std::shared_ptr<AudioSink>& audioSink);
  void onSetVideoSink(const std::shared_ptr<VideoSink>& videoSink);
  void onQueueBuffer(const std::shared_ptr<Message>& msg);

  void onRenderAudio(const std::shared_ptr<AudioFrame>& audioFrame);
  void onRenderVideo(const std::shared_ptr<VideoFrame>& videoFrame);

  void onMessageReceived(const std::shared_ptr<Message>& message) override;

  std::shared_ptr<Looper> mLooper;
  std::shared_ptr<Message> mNotify;
  std::shared_ptr<AudioSink> mAudioSink;
  std::shared_ptr<VideoSink> mVideoSink;
};
} /* namespace avp */

#endif /* !AVP_RENDER_SYNCHRONIZER_H */
