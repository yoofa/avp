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
#include "media/media_defs.h"
#include "media/media_errors.h"
#include "media/message.h"
#include "player/default_decoder_factory.h"

#ifdef AVP_FFMPEG_DECODER
#include "decoder/ffmpeg_decoder.h"
#include "decoder/ffmpeg_decoder_factory.h"
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

void AvpDecoder::start() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatStart, shared_from_this()));
  msg->post();
}

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
  AVE_CHECK(format.get() != nullptr);
  AVE_CHECK(mDecoder.get() == nullptr);

  std::string mime;
  AVE_CHECK(format->findString("mime", mime));

  AVE_LOG(LS_INFO) << "onConfigure, mime:" << mime;

  mIsAudio = !strncasecmp("audio/", mime.c_str(), 6);
  CodecType codecType = mimeToCodec(mime.c_str());

  if (codecType == CODEC_UNKNOWN) {
    AVE_LOG(LS_ERROR) << "unknown codec, mime:" << mime;
    handleError(ERROR_UNSUPPORTED);
    return;
  }

  mDecoder = mDecoderFactory->createDecoder(mIsAudio, codecType);

  if (mDecoder == nullptr || mDecoder.get() == nullptr) {
    AVE_LOG(LS_ERROR) << "decoder create failed, mime:" << mime;

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

  mDecoder->setCallback(static_cast<Decoder::DecoderCallback*>(this));
}

void AvpDecoder::onStart() {
  AVE_LOG(LS_INFO) << "onStart";
  if (mDecoder == nullptr) {
    AVE_LOG(LS_ERROR) << "Failed to start decoder, no support decoder";
    handleError(UNKNOWN_ERROR);
    return;
  }

  status_t err = mDecoder->start();

  if (err != OK) {
    AVE_LOG(LS_ERROR) << "Failed to start decoder, err:" << err;
    mDecoder.reset();
    handleError(err);
  }

  onRequestInputBuffers();
}
void AvpDecoder::onPause() {}
void AvpDecoder::onResume() {}
void AvpDecoder::onFlush() {}
void AvpDecoder::onShutdown() {}

/////////////////////

bool AvpDecoder::isSated() {
  return mInputBufferMessageQueue.size() > 5;
}
// fetch buffer and save it in message
status_t AvpDecoder::fetchInputBuffer(std::shared_ptr<Message>& message) {
  std::shared_ptr<Buffer> accessUnit;
  bool drop = true;
  do {
    status_t err = mSource->dequeueAccessUnit(mIsAudio, accessUnit);

    if (err == WOULD_BLOCK) {
      return err;
    } else if (err != OK) {
      return err;
    }

    drop = false;
    // TODO(youfa) drop: no need to decode if too late
  } while (drop);
  int64_t timeUs;
  accessUnit->meta()->findInt64("timeUs", &timeUs);

  //  AVE_LOG(LS_INFO) << " fetchInputBuffer, pts:" << timeUs;

  message->setBuffer("buffer", std::move(accessUnit));
  return OK;
}

bool AvpDecoder::onInputBufferFetched(const std::shared_ptr<Message>& message) {
  std::shared_ptr<Buffer> buffer;
  bool hasBuffer = message->findBuffer("buffer", buffer);
  if (!buffer.get()) {
    status_t streamErr = ERROR_END_OF_STREAM;
    AVE_CHECK(message->findInt32("err", &streamErr) || !hasBuffer);
    AVE_CHECK(streamErr != OK);
  } else {
    status_t err;
    err = mDecoder->queueInputBuffer(buffer);
    if (err != OK) {
      if (err == ERROR_RETRY) {
        return false;
      }
      AVE_LOG(LS_ERROR) << "onInputBufferFetched: queueInputBuffer failed:"
                        << err;
      handleError(err);
    }
  }
  return true;
}

bool AvpDecoder::doRequestInputBuffers() {
  status_t err = OK;

  while (err == OK && !isSated()) {
    auto message = std::make_shared<Message>();
    err = fetchInputBuffer(message);

    if (err != OK && err != ERROR_END_OF_STREAM) {
      break;
    }

    if (!mInputBufferMessageQueue.empty() || !onInputBufferFetched(message)) {
      mInputBufferMessageQueue.push_back(std::move(message));
    }
  }

  return err == WOULD_BLOCK;
}
void AvpDecoder::onRequestInputBuffers() {
  if (mRequestInputBuffersPending) {
    return;
  }

  if (doRequestInputBuffers()) {
    mRequestInputBuffersPending = true;

    std::shared_ptr<Message> msg(std::make_shared<Message>(
        kWhatRequestInputBuffers, shared_from_this()));

    // TODO(youfa) use frame rate
    msg->post(10 * 1000LL);
  }
}

void AvpDecoder::handleAnInputBuffer() {
  while (!mInputBufferMessageQueue.empty()) {
    if (!onInputBufferFetched(*mInputBufferMessageQueue.begin())) {
      break;
    }
    mInputBufferMessageQueue.erase(mInputBufferMessageQueue.begin());
  }

  onRequestInputBuffers();
}

void AvpDecoder::handleAnOutputBuffer() {
  std::shared_ptr<Buffer> buffer;
  status_t err = mDecoder->dequeueOutputBuffer(buffer, 10 * 1000LL);
  if (buffer.get() == nullptr) {
    if (err != TIMED_OUT) {
      handleError(err);
      return;
    }
    return;
  }

  auto renderMsg =
      std::make_shared<Message>(kWhatRenderBuffer, shared_from_this());
  renderMsg->setBuffer("buffer", buffer);

  mRender->queueBuffer(mIsAudio, buffer, renderMsg);
}

void AvpDecoder::onInputBufferAvailable() {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatInputBufferAvailable,
                                       shared_from_this());
  msg->post();
}
void AvpDecoder::onOutputBufferAvailable() {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatOutputBufferAvailable,
                                       shared_from_this());
  msg->post();
}
void AvpDecoder::onFormatChange(std::shared_ptr<Message> format) {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatDecodingFormatChange,
                                       shared_from_this());
  msg->setMessage("message", std::move(format));
  msg->post();
}
void AvpDecoder::onError(status_t error) {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatDecodingError,
                                       shared_from_this());
  msg->setInt32("err", error);
  msg->post();
}

void AvpDecoder::onMessageReceived(const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatConfigure: {
      std::shared_ptr<Message> format;
      AVE_CHECK(msg->findMessage("format", format));
      onConfigure(format);
      break;
    }
    case kWhatSetParameters: {
      break;
    }
    case kWhatSetRenderer: {
      std::shared_ptr<MessageObject> obj;
      AVE_CHECK(msg->findObject("render", obj));
      auto render = std::dynamic_pointer_cast<AvpRenderSynchronizer>(obj);
      mRender = render;
      break;
    }
    case kWhatSetVideoSink: {
      std::shared_ptr<MessageObject> obj;
      AVE_CHECK(msg->findObject("videoSink", obj));
      auto sink = std::dynamic_pointer_cast<VideoSink>(obj);
      mVideoSink = sink;
      break;
    }

    case kWhatRequestInputBuffers: {
      mRequestInputBuffersPending = false;
      onRequestInputBuffers();
      break;
    }
    case kWhatStart: {
      onStart();
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

    case kWhatRenderBuffer: {
      break;
    }

    case kWhatInputBufferAvailable: {
      handleAnInputBuffer();
      break;
    }
    case kWhatOutputBufferAvailable: {
      handleAnOutputBuffer();
      break;
    }
    case kWhatDecodingFormatChange: {
      break;
    }
    case kWhatDecodingError: {
      break;
    }

    default:
      break;
  }
}

} /* namespace avp */
