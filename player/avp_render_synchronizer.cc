/*
 * avp_render_synchronizer.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avp_render_synchronizer.h"
#include <memory>

#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "media/buffer.h"

namespace avp {

AvpRenderSynchronizer::AvpRenderSynchronizer(
    std::shared_ptr<Message> msg,
    std::shared_ptr<Looper> looper,
    std::shared_ptr<MediaClock> mediaClock)
    : mNotify(std::move(msg)),
      mLooper(std::move(looper)),
      mMediaClock(std::move(mediaClock)),
      mPlaybackRate(1.0),
      mAudioFirstAnchorTimeMediaUs(-1),
      mAnchorTimeMediaUs(-1),
      mAnchorNumFramesWritten(-1),
      mVideoLateByUs(0LL),
      mNextVideoTimeMediaUs(-1),
      mHasAudio(false),
      mHasVideo(false),
      mUseAudioCallback(false),
      mPaused(false),
      mFirstVideoFrameReceived(false) {
  mMediaClock->setPlaybackRate(mPlaybackRate);
  mMediaClock->setFreeRun(true);
}

AvpRenderSynchronizer::~AvpRenderSynchronizer() {}

void AvpRenderSynchronizer::init() {
  mLooper->registerHandler(shared_from_this());
}

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
  auto msg = std::make_shared<Message>(kWhatQueueBuffer, shared_from_this());
  msg->setInt32("audio", static_cast<int32_t>(audio));
  msg->setBuffer("buffer", std::move(buffer));
  msg->setMessage("renderMessage", std::move(renderMessage));
  msg->post();
}

void AvpRenderSynchronizer::pause() {}

void AvpRenderSynchronizer::resume() {}

void AvpRenderSynchronizer::flush() {}

///////////////////////////////////

int64_t AvpRenderSynchronizer::getRealTimeUs(int64_t mediaTimeUs,
                                             int64_t nowUs) {
  int64_t realUs;
  if (mMediaClock->getRealTimeFor(mediaTimeUs, &realUs) != OK) {
    return nowUs;
  }

  return realUs;
}

void AvpRenderSynchronizer::onSetAudioSink(
    const std::shared_ptr<AudioSink>& sink) {
  mAudioSink = sink;
}
void AvpRenderSynchronizer::onSetVideoSink(
    const std::shared_ptr<VideoSink>& sink) {
  mVideoSink = sink;
}

void AvpRenderSynchronizer::onQueueBuffer(const std::shared_ptr<Message>& msg) {
  // TODO(youfa) do not enqueue if msg is old

  int32_t audio;
  AVE_CHECK(msg->findInt32("audio", &audio));

  if (audio) {
    mHasAudio = true;
  } else {
    mHasVideo = true;
  }

  std::shared_ptr<Buffer> buffer;
  AVE_CHECK(msg->findBuffer("buffer", buffer));

  std::shared_ptr<Message> render_message;
  AVE_CHECK(msg->findMessage("renderMessage", render_message));

  // int64_t time_us;
  // AVE_CHECK(buffer->meta()->findInt64("timeUs", &time_us));
  // AVE_LOG(LS_INFO) << "onQueueBuffer, ts:" << time_us;

  QueueEntry entry;
  entry.mBuffer = std::move(buffer);
  entry.mConsumedNotify = std::move(render_message);

  if (audio) {
    mAudioQueue.push(std::move(entry));
    postDrainAudio();
  } else {
    mVideoQueue.push(std::move(entry));
    postDrainVideo();
  }

  // TODO(youfa) remove some audio frame if audio is too fast than video

  // if (!audio && mVideoSink) {
  //   // AVE_LOG(LS_INFO) << "onQueueBuffer video:";
  //   mVideoSink->onFrame(buffer);
  // } else if (audio && mAudioSink) {
  //   mAudioSink->onFrame(buffer);
  // }
}

void AvpRenderSynchronizer::postDrainAudio() {
  if (mDrainAudioQueuePending || mUseAudioCallback) {
    return;
  }

  if (mAudioQueue.empty()) {
    return;
  }
}

void AvpRenderSynchronizer::onRenderAudio(
    const std::shared_ptr<AudioFrame>& frame) {}

void AvpRenderSynchronizer::postDrainVideo() {
  if (mDrainVideoQueuePending || (mPaused && mFirstVideoFrameReceived)) {
    return;
  }

  if (mVideoQueue.empty()) {
    return;
  }

  QueueEntry& entry = mVideoQueue.front();

  auto msg = std::make_shared<Message>(kWhatRenderVideo, shared_from_this());

  // eos does not carry a buffer, notify asap
  if (entry.mBuffer == nullptr) {
    mDrainVideoQueuePending = true;
    msg->post();
    return;
  }

  int64_t now = Looper::getNowUs();

  int64_t time_us;
  AVE_CHECK(entry.mBuffer->meta()->findInt64("timeUs", &time_us));
  {
    // TODO(youfa) with lock
    if (mAnchorTimeMediaUs < 0) {
      mAnchorTimeMediaUs = time_us;
      // commit anchor time
      mMediaClock->updateAnchor(time_us, now, time_us);
    }
  }

  if (!mFirstVideoFrameReceived || time_us < mAudioFirstAnchorTimeMediaUs) {
    // first video frame, render asap
    msg->post();
  } else {
    //  render before the latency
    int64_t render_latency = 0;
    if (mVideoSink) {
      render_latency = mVideoSink->render_latency();
    }
    mMediaClock->addTimer(msg, time_us, render_latency);
  }
}

void AvpRenderSynchronizer::onRenderVideo(
    const std::shared_ptr<VideoFrame>& frame) {
  if (mVideoSink) {
    mFirstVideoFrameReceived = true;
    QueueEntry entry = mVideoQueue.front();
    mVideoQueue.pop();

    int64_t time_us;
    int64_t now = Looper::getNowUs();
    int64_t now_media_us;
    mMediaClock->getMediaTime(now, &now_media_us, true);
    AVE_CHECK(entry.mBuffer->meta()->findInt64("timeUs", &time_us));
    // AVE_LOG(LS_INFO) << "onRenderVideo, ts:" << time_us / 1000
    //              << "ms, now media time:" << now_media_us / 1000
    //              << "ms, now:" << now;
    mVideoSink->onFrame(entry.mBuffer);
  }
}

void AvpRenderSynchronizer::onMessageReceived(
    const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatSetAudioSink: {
      std::shared_ptr<MessageObject> obj;
      AVE_CHECK(msg->findObject("audioSink", obj));
      onSetAudioSink(std::dynamic_pointer_cast<AudioSink>(obj));

      break;
    }
    case kWhatSetVideoSink: {
      std::shared_ptr<MessageObject> obj;
      AVE_CHECK(msg->findObject("videoSink", obj));
      onSetVideoSink(std::dynamic_pointer_cast<VideoSink>(obj));
      break;
    }
    case kWhatQueueBuffer: {
      onQueueBuffer(msg);
      break;
    }
    case kWhatRenderAudio: {
      break;
    }
    case kWhatRenderVideo: {
      onRenderVideo(nullptr);
      break;
    }
    default:
      break;
  }
}

} /* namespace avp */
