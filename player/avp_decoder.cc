/*
 * avp_decoder.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avp_decoder.h"

#include <memory>

#include "base/checks.h"
#include "base/logging.h"
#include "common/media_errors.h"
#include "common/message.h"
#include "player/default_decoder_factory.h"

#ifdef AVP_FFMPEG_DECODER
#include "video/decoder/ffmpeg_decoder.h"
#include "video/decoder/ffmpeg_decoder_factory.h"
#endif

namespace avp {

AvpDecoder::AvpDecoder(std::shared_ptr<Message> notify,
                       std::shared_ptr<PlayerBase::ContentSource> source,
                       std::shared_ptr<AvpRenderSynchronizer> render,
                       std::shared_ptr<VideoSink> videoSink)
    : mLooper(std::make_shared<Looper>()),
      mNotify(notify),
      mRequestInputBuffersPending(false),
      mSource(std::move(source)),
      mRender(render),
      mVideoSink(videoSink),
#ifdef AVP_FFMPEG_DECODER
      mDecoderFactory(std::make_unique<FFmpegDecoderFactory>())
#else
      mDecoderFactory(std::make_unique<DefaultDecoderFactory>())
#endif
{
  mLooper->setName("AvpDecoder");
}

AvpDecoder::~AvpDecoder() {
  mLooper->unregisterHandler(id());
  mLooper->stop();
}

void AvpDecoder::init() {
  mLooper->start();
  mLooper->registerHandler(shared_from_this());
}

void AvpDecoder::configure(const std::shared_ptr<Message>& format) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatConfigure, shared_from_this()));
  msg->setMessage("format", format);
  msg->post();
}

// set parameters on the fly
void AvpDecoder::setParameters(const std::shared_ptr<Message>& parameters) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatSetParameters, shared_from_this()));
  msg->setMessage("format", parameters);
  msg->post();
}
void AvpDecoder::setRender(
    const std::shared_ptr<AvpRenderSynchronizer> render) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatSetRenderer, shared_from_this()));
  msg->setObject("render", std::move(render));
  msg->post();
}
void AvpDecoder::setVideoSink(const std::shared_ptr<VideoSink> sink) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatSetRenderer, shared_from_this()));
  msg->setObject("videoSink", std::move(sink));
  msg->post();
}

//  void AvpDecoder::requestInputBuffers(std::shared_ptr<Message> & format) {
//    std::shared_ptr<Message> msg(
//        std::make_shared<Message>(kWhatConfigure, shared_from_this()));
//    msg->setMessage("format", format);
//    msg->post();
//  }

void AvpDecoder::pause() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatPause, shared_from_this()));
  msg->post();
}
void AvpDecoder::resume() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatResume, shared_from_this()));
  msg->post();
}
void AvpDecoder::flush() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatFlush, shared_from_this()));
  msg->post();
}
void AvpDecoder::shutdown() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatShutdown, shared_from_this()));
  msg->post();
}

void AvpDecoder::handleError(status_t err) {
  auto notify = mNotify->dup();
  notify->setInt32("what", kWhatError);
  notify->setInt32("err", err);
  notify->post();
}

//////////////////////////////////////////

void AvpDecoder::onConfigure(const std::shared_ptr<Message>& format) {
  CHECK(format.get() != nullptr);
  CHECK(mDecoder.get() == nullptr);

  std::string mime;
  CHECK(format->findString("mime", mime));

  LOG(LS_INFO) << "onConfigure, mime:" << mime;

  mIsAudio = !strncasecmp("audio/", mime.c_str(), 6);

  mDecoder = mDecoderFactory->createDecoder(mime.c_str());

  if (mDecoder == nullptr || mDecoder.get() == nullptr) {
    LOG(LS_ERROR) << "notsupport, mime:" << mime;

    handleError(ERROR_UNSUPPORTED);
    return;
  }

  status_t err;
  err = mDecoder->configure(format);

  if (err != OK) {
    mDecoder.reset();
    handleError(err);
    return;
  }

  mDecoder->setCallback(this);

  err = mDecoder->start();
  if (err != OK) {
    LOG(LS_ERROR) << "Failed to start decoder [" << mime.c_str()
                  << "], err:" << err;
    mDecoder.reset();
    handleError(err);
  }
}

bool AvpDecoder::doRequestInputBuffers() {
  return true;
}
void AvpDecoder::onRequestInputBuffers() {
  if (mRequestInputBuffersPending) {
    return;
  }

  if (doRequestInputBuffers()) {
    mRequestInputBuffersPending = true;

    std::shared_ptr<Message> msg(std::make_shared<Message>(
        kWhatRequestInputBuffers, shared_from_this()));
    msg->post(10 * 1000LL);
  }
}

void AvpDecoder::onPause() {}
void AvpDecoder::onResume() {}
void AvpDecoder::onFlush() {}
void AvpDecoder::onShutdown() {}

void AvpDecoder::onMessageReceived(const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatConfigure: {
      std::shared_ptr<Message> format;
      CHECK(msg->findMessage("format", format));
      onConfigure(format);
      break;
    }
    case kWhatSetParameters: {
      break;
    }
    case kWhatSetRenderer: {
      std::shared_ptr<MessageObject> obj;
      CHECK(msg->findObject("render", obj));
      auto render = std::dynamic_pointer_cast<AvpRenderSynchronizer>(obj);
      mRender = render;
      break;
    }
    case kWhatSetVideoSink: {
      std::shared_ptr<MessageObject> obj;
      CHECK(msg->findObject("videoSink", obj));
      auto sink = std::dynamic_pointer_cast<VideoSink>(obj);
      mVideoSink = sink;
      break;
    }

    case kWhatRequestInputBuffers: {
      break;
    }

    case kWhatPause: {
      break;
    }

    case kWhatResume: {
      break;
    }
    case kWhatFlush: {
      break;
    }
    case kWhatShutdown: {
      break;
    }
    default:
      break;
  }
}

} /* namespace avp */
