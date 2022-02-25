/*
 * avplayer.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avplayer.h"

#include <iostream>

#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "common/message.h"
#include "player/default_Audio_decoder_factory.h"
#include "player/default_video_decoder_factory.h"

#include "generic_source.h"

namespace avp {

AvPlayer::AvPlayer()
    : AvPlayer(std::make_shared<DefaultAudioDecoderFactory>(),
               std::make_shared<DefaultVideoDecoderFactory>()) {}

AvPlayer::AvPlayer(std::shared_ptr<AudioDecoderFactory> audioDecoderFactory,
                   std::shared_ptr<VideoDecoderFactory> videoDecoderFactory)
    : mAudioDecoderFactory(std::move(audioDecoderFactory)),
      mVideoDecoderFactory(std::move(videoDecoderFactory)),
      mPlayerLooper(std::make_shared<Looper>()) {
  mPlayerLooper->setName("AvPlayer");
}

AvPlayer::~AvPlayer() {}

status_t AvPlayer::setListener(const std::shared_ptr<Listener>& listener) {
  mListener = listener;
  return 0;
}

status_t AvPlayer::init() {
  mPlayerLooper->start();
  mPlayerLooper->registerHandler(shared_from_this());
  return 0;
}

status_t AvPlayer::setDataSource(const char* url) {
  std::shared_ptr<ContentSource> source;
  {
    std::shared_ptr<GenericSource> genericSource(
        std::make_shared<GenericSource>());
    genericSource->setDataSource(url);
    source = std::move(genericSource);
  }

  return setDataSource(source);
}

status_t AvPlayer::setDataSource(int fd, int64_t offset, int64_t length) {
  std::shared_ptr<ContentSource> source;
  {
    std::shared_ptr<GenericSource> genericSource(
        std::make_shared<GenericSource>());
    genericSource->setDataSource(fd, offset, length);
    source = std::move(genericSource);
  }

  return setDataSource(source);
}

status_t AvPlayer::setDataSource(const std::shared_ptr<ContentSource>& source) {
  auto msg = std::make_shared<Message>(kWhatSetDataSource, shared_from_this());
  auto notify =
      std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  source->setNotifier(notify);

  msg->setObject("source", std::static_pointer_cast<MessageObject>(source));
  msg->post(0);
  return OK;
}

status_t AvPlayer::setAudioRender(std::shared_ptr<AudioSink> render) {
  return 0;
}

status_t AvPlayer::setVideoRender(std::shared_ptr<VideoSink> videorender) {
  return 0;
}

status_t AvPlayer::prepare() {
  auto msg = std::make_shared<Message>(kWhatPrepare, shared_from_this());
  msg->post(0);
  return 0;
}

status_t AvPlayer::start() {
  return 0;
}

status_t AvPlayer::stop() {
  return OK;
}

status_t AvPlayer::pause() {
  return OK;
}

status_t AvPlayer::resume() {
  return OK;
}

status_t AvPlayer::seekTo(int msec, SeekMode seekMode) {
  return OK;
}

status_t AvPlayer::reset() {
  auto msg = std::make_shared<Message>(kWhatReset, shared_from_this());
  msg->post(0);
  return OK;
}

/************* player action *************************/
void AvPlayer::performReset() {
  mSource.reset();
}

/************* from content source  *************************/
void AvPlayer::onSourceNotify(const std::shared_ptr<Message>& msg) {
  int32_t what;
  CHECK(msg->findInt32("what", &what));
  switch (what) {
    case ContentSource::kWhatPrepared: {
      LOG(LS_INFO) << "source prepared";
      break;
    }
    default:
      break;
  }
}

void AvPlayer::onMessageReceived(const std::shared_ptr<Message>& message) {
  LOG(LS_DEBUG) << "AvPlayer::onMessageReceived:" << message->what();
  switch (message->what()) {
      /************* from avplayer ***************/
    case kWhatSetDataSource: {
      std::shared_ptr<MessageObject> obj;
      message->findObject("source", obj);
      mSource = std::dynamic_pointer_cast<ContentSource>(obj);
      notifyListner(kWhatSetDataSourceCompleted, nullptr);
      break;
    }

    case kWhatPrepare: {
      mSource->prepare();
      break;
    }

    case kWhatReset: {
      performReset();
    } break;

      /************* from contentsource ***************/
    case kWhatSourceNotify: {
      onSourceNotify(message);
      break;
    }
    default:
      break;
  }
}

}  // namespace avp
