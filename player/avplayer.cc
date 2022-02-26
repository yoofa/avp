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
    : mPlayerLooper(std::make_shared<Looper>()),
      mStarted(false),
      mPrepared(false),
      mPaused(false),
      mSourceStarted(false),
      mScanSourcesPendding(false) {
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
  msg->post();
  return OK;
}

status_t AvPlayer::setAudioSink(std::shared_ptr<AudioSink> sink) {
  auto msg = std::make_shared<Message>(kWhatSetAudioSink, shared_from_this());
  msg->setObject("audioSink", std::move(sink));
  msg->post();
  return OK;
}

status_t AvPlayer::setVideoSink(std::shared_ptr<VideoSink> sink) {
  auto msg = std::make_shared<Message>(kWhatSetVideoSink, shared_from_this());
  msg->setObject("videoSink", std::move(sink));
  msg->post();
  return OK;
}

status_t AvPlayer::prepare() {
  auto msg = std::make_shared<Message>(kWhatPrepare, shared_from_this());
  msg->post();
  return OK;
}

status_t AvPlayer::start() {
  auto msg = std::make_shared<Message>(kWhatStart, shared_from_this());
  msg->post();
  return OK;
}

status_t AvPlayer::stop() {
  return OK;
}

status_t AvPlayer::pause() {
  auto msg = std::make_shared<Message>(kWhatPause, shared_from_this());
  msg->post();
  return OK;
}

status_t AvPlayer::resume() {
  auto msg = std::make_shared<Message>(kWhatResume, shared_from_this());
  msg->post();
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

///////////////////////////////////////////

void AvPlayer::postScanSources() {
  if (mScanSourcesPendding) {
    return;
  }
  auto msg = std::make_shared<Message>(kWhatScanSources, shared_from_this());
  msg->post();
  mScanSourcesPendding = true;
}

status_t AvPlayer::instantiateDecoder(bool audio,
                                      std::shared_ptr<AvpDecoder>& decoder) {
  if (decoder.get() != nullptr) {
    return OK;
  }

  auto format = mSource->getFormat(audio);
  LOG(LS_INFO) << "instantiateDecoder format: " << format.get();

  if (format.get() == nullptr) {
    return UNKNOWN_ERROR;
  }

  std::string mime;
  CHECK(format->findString("mime", mime));
  LOG(LS_INFO) << "instantiateDecoder mime: " << mime;

  if (audio) {
    auto notify =
        std::make_shared<Message>(kWhatAudioNotify, shared_from_this());
    decoder = std::make_shared<AvpDecoder>(notify, mSource);
  } else {
    auto notify =
        std::make_shared<Message>(kWhatVideoNotify, shared_from_this());
    decoder =
        std::make_shared<AvpDecoder>(notify, mSource, mRender, mVideoSink);
  }

  decoder->init();

  decoder->configure(format);

  return OK;
}

///////////////////////////////////////////

void AvPlayer::onStart(int64_t startUs, SeekMode seekMode) {
  LOG(LS_INFO) << "onStart";
  if (!mSourceStarted) {
    mSource->start();
    mSourceStarted = true;
  }
  if (startUs > 0) {
    // TODO(youfa) performSeek
  }

  mStarted = true;
  mPaused = true;

  auto renderNotify(
      std::make_shared<Message>(kWhatRendererNotify, shared_from_this()));
  mRender =
      std::make_shared<AvpRenderSynchronizer>(renderNotify, mPlayerLooper);

  mRender->setAudioSink(mAudioSink);
  mRender->setVideoSink(mVideoSink);

  postScanSources();
}

void AvPlayer::onStop() {}

void AvPlayer::onPause() {}

void AvPlayer::onResume() {}

void AvPlayer::onSeek() {}

void AvPlayer::performReset() {
  mSource.reset();
}

void AvPlayer::onSourceNotify(const std::shared_ptr<Message>& msg) {
  int32_t what;
  CHECK(msg->findInt32("what", &what));
  switch (what) {
    case ContentSource::kWhatPrepared: {
      LOG(LS_INFO) << "source prepared: " << mSource.get();
      if (mSource.get() == nullptr) {
        return;
      }
      int32_t err;
      CHECK(msg->findInt32("err", &err));
      if (err != OK) {
      } else {
        mPrepared = true;
      }

      // TODO(youfa) new msg here
      notifyListner(kWhatPrepared, msg);

      break;
    }
    default:
      break;
  }
}
void AvPlayer::onDecoderNotify(const std::shared_ptr<Message>& msg) {
  int32_t what;
  CHECK(msg->findInt32("what", &what));
  switch (what) {
    case AvpDecoder::kWhatInputDiscontinuity: {
      break;
    }
    case AvpDecoder::kWhatVideoSizeChanged: {
      break;
    }
    case AvpDecoder::kWhatEOS: {
      break;
    }
    case AvpDecoder::kWhatError: {
      break;
    }
    case AvpDecoder::kWhatFlushCompleted: {
      break;
    }
    case AvpDecoder::kWhatResumeCompleted: {
      break;
    }
    case AvpDecoder::kWhatShutdownCompleted: {
      break;
    }
    default:
      break;
  }
}

void AvPlayer::onRenderNotify(const std::shared_ptr<Message>& msg) {}

void AvPlayer::onMessageReceived(const std::shared_ptr<Message>& message) {
  LOG(LS_DEBUG) << "AvPlayer::onMessageReceived:" << message->what();
  switch (message->what()) {
      /************* from avplayer ***************/
    case kWhatSetDataSource: {
      std::shared_ptr<MessageObject> obj;
      message->findObject("source", obj);
      mSource = std::dynamic_pointer_cast<ContentSource>(obj);
      notifyListner(kWhatSetDataSourceCompleted, std::make_shared<Message>());
      break;
    }

    case kWhatSetAudioSink: {
      std::shared_ptr<MessageObject> obj;
      message->findObject("audioSink", obj);
      mAudioSink = std::dynamic_pointer_cast<AudioSink>(obj);
      break;
    }
    case kWhatSetVideoSink: {
      std::shared_ptr<MessageObject> obj;
      message->findObject("videoSink", obj);
      mVideoSink = std::dynamic_pointer_cast<VideoSink>(obj);
      if (mVideoDecoder.get() != nullptr) {
        mVideoDecoder->setVideoSink(mVideoSink);
      }
      break;
    }

    case kWhatPrepare: {
      mSource->prepare();
      break;
    }

    case kWhatStart: {
      onStart();
      break;
    }

    case kWhatScanSources: {
      LOG(LS_INFO) << "kWhatScanSources";
      mScanSourcesPendding = false;

      bool rescan = false;
      if (/*mVideoSink.get() != nullptr &&*/ mVideoDecoder.get() == nullptr) {
        if (instantiateDecoder(false, mVideoDecoder) == WOULD_BLOCK) {
          rescan = true;
        }
      }

      if (/*mAudioSink.get() != nullptr &&*/ mAudioDecoder.get() == nullptr) {
        if (instantiateDecoder(true, mAudioDecoder) == WOULD_BLOCK) {
          rescan = true;
        }
      }

      if (rescan) {
        message->post(1000000LL);
        mScanSourcesPendding = true;
      }
      break;
    }

    case kWhatSeek: {
      break;
    }

    case kWhatPause: {
      break;
    }

    case kWhatResume: {
      break;
    }

    case kWhatReset: {
      performReset();
    } break;

    case kWhatSourceNotify: {
      onSourceNotify(message);
      break;
    }
    case kWhatAudioNotify:
    case kWhatVideoNotify: {
      onDecoderNotify(message);
      break;
    }

    case kWhatRendererNotify: {
      onRenderNotify(message);
      break;
    }
    default:
      break;
  }
}

}  // namespace avp
