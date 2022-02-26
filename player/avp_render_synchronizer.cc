/*
 * avp_render_synchronizer.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avp_render_synchronizer.h"

#include "base/checks.h"
#include "base/errors.h"

namespace avp {

AvpRenderSynchronizer::AvpRenderSynchronizer(std::shared_ptr<Message> msg,
                                             std::shared_ptr<Looper> looper)
    : mLooper(std::move(looper)), mNotify(std::move(msg)) {}

AvpRenderSynchronizer::~AvpRenderSynchronizer() {}

void AvpRenderSynchronizer::setAudioSink(
    const std::shared_ptr<AudioSink> audioSink) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatSetAudioSink, shared_from_this()));
  msg->setObject("audioSink", std::move(audioSink));
  msg->post();
}

void AvpRenderSynchronizer::setVideoSink(
    const std::shared_ptr<VideoSink> videoSink) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatSetVideoSink, shared_from_this()));
  msg->setObject("videoSink", std::move(videoSink));
  msg->post();
}

void AvpRenderSynchronizer::queueBuffer(
    bool audio,
    const std::shared_ptr<Buffer> buffer,
    const std::shared_ptr<Message> renderMessage) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatQueueBuffer, shared_from_this()));
  msg->setInt32("audio", static_cast<int32_t>(audio));
  msg->setBuffer("buffer", std::move(buffer));
  msg->setMessage("renderMessage", std::move(renderMessage));
  msg->post();
}

///////////////////////////////////
void AvpRenderSynchronizer::onSetAudioSink(
    const std::shared_ptr<AudioSink>& sink) {
  mAudioSink = sink;
}
void AvpRenderSynchronizer::onSetVideoSink(
    const std::shared_ptr<VideoSink>& sink) {
  mVideoSink = sink;
}
void AvpRenderSynchronizer::onQueueBuffer(const std::shared_ptr<Message>& msg) {
}

void AvpRenderSynchronizer::onRenderAudio(
    const std::shared_ptr<AudioFrame>& frame) {}

void AvpRenderSynchronizer::onRenderVideo(
    const std::shared_ptr<VideoFrame>& frame) {}

void AvpRenderSynchronizer::onMessageReceived(
    const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatSetAudioSink: {
      std::shared_ptr<MessageObject> obj;
      CHECK(msg->findObject("audioSink", obj));
      onSetAudioSink(std::dynamic_pointer_cast<AudioSink>(obj));

      break;
    }
    case kWhatSetVideoSink: {
      std::shared_ptr<MessageObject> obj;
      CHECK(msg->findObject("videoSink", obj));
      onSetVideoSink(std::dynamic_pointer_cast<VideoSink>(obj));
      break;
    }
    case kWhatQueueBuffer: {
      onQueueBuffer(msg);
      break;
    }
    default:
      break;
  }
}

} /* namespace avp */
