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
#include "media/codec/codec_id.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/message.h"

namespace ave {
namespace player {

using ave::media::CodecId;

AvpDecoder::AvpDecoder(std::shared_ptr<Message> notify,
                       std::shared_ptr<ContentSource> source,
                       std::shared_ptr<AVSynchronizeRender> render,
                       std::shared_ptr<VideoSink> videoSink)
    : looper_(std::make_shared<Looper>()),
      notify_(notify),
      request_input_buffers_pending_(false),
      source_(std::move(source)),
      render_(render),
      video_sink_(videoSink) {
  looper_->setName("AvpDecoder");
}

AvpDecoder::~AvpDecoder() {
  looper_->unregisterHandler(id());
  looper_->stop();
}

void AvpDecoder::Init() {
  looper_->start();
  looper_->registerHandler(shared_from_this());
}

void AvpDecoder::Configure(const std::shared_ptr<Message>& format) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatConfigure, shared_from_this()));
  msg->setMessage("format", format);
  msg->post();
}

// set parameters on the fly
void AvpDecoder::SetParameters(const std::shared_ptr<Message>& parameters) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatSetParameters, shared_from_this()));
  msg->setMessage("format", parameters);
  msg->post();
}
void AvpDecoder::SetRender(const std::shared_ptr<AVSynchronizeRender> render) {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatSetRenderer, shared_from_this()));
  msg->setObject("render", std::move(render));
  msg->post();
}
void AvpDecoder::SetVideoSink(const std::shared_ptr<VideoSink> sink) {
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

void AvpDecoder::Start() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatStart, shared_from_this()));
  msg->post();
}

void AvpDecoder::Pause() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatPause, shared_from_this()));
  msg->post();
}
void AvpDecoder::Resume() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatResume, shared_from_this()));
  msg->post();
}
void AvpDecoder::Flush() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatFlush, shared_from_this()));
  msg->post();
}
void AvpDecoder::Shutdown() {
  std::shared_ptr<Message> msg(
      std::make_shared<Message>(kWhatShutdown, shared_from_this()));
  msg->post();
}

void AvpDecoder::HandleError(status_t err) {
  auto notify = notify_->dup();
  notify->setInt32("what", kWhatError);
  notify->setInt32("err", err);
  notify->post();
}

//////////////////////////////////////////

void AvpDecoder::OnConfigure(const std::shared_ptr<Message>& format) {
  AVE_CHECK(format.get() != nullptr);
  AVE_CHECK(decoder_ == nullptr);

  std::string mime;
  AVE_CHECK(format->findString("mime", mime));

  AVE_LOG(LS_INFO) << "onConfigure, mime:" << mime;

  is_audio_ = !strncasecmp("audio/", mime.c_str(), 6);
  auto codec_id = ave::media::MimeToCodecId(mime.c_str());

  if (codec_id == CodecId::AVE_CODEC_ID_NONE) {
    AVE_LOG(LS_ERROR) << "unknown codec, mime:" << mime;
    HandleError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  decoder_ = codec_factory_->CreateCodecByType(codec_id, false);

  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "decoder create failed, mime:" << mime;

    HandleError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  auto config = std::make_shared<ave::media::CodecConfig>();

  status_t err;
  err = decoder_->Configure(config);

  if (err != ave::OK) {
    decoder_.reset();
    HandleError(err);
    return;
  }

  decoder_->SetCallback(this);
}

void AvpDecoder::OnStart() {
  AVE_LOG(LS_VERBOSE) << "onStart";
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "Failed to start decoder, no support decoder";
    HandleError(ave::UNKNOWN_ERROR);
    return;
  }

  status_t err = decoder_->Start();

  if (err != ave::OK) {
    AVE_LOG(LS_ERROR) << "Failed to start decoder, err:" << err;
    decoder_.reset();
    HandleError(err);
  }

  OnRequestInputBuffers();
}
void AvpDecoder::OnPause() {}
void AvpDecoder::OnResume() {}
void AvpDecoder::OnFlush() {}
void AvpDecoder::OnShutdown() {}

/////////////////////

bool AvpDecoder::IsSated() {
  return input_packet_queue_.size() > 5;
}
// fetch buffer and save it in message
status_t AvpDecoder::FetchInputBuffer(std::shared_ptr<Message>& message) {
  std::shared_ptr<ave::media::MediaPacket> access_unit;
  bool drop = true;
  do {
    status_t err = source_->DequeueAccessUnit(
        is_audio_ ? MediaType::AUDIO : MediaType::VIDEO, access_unit);

    if (err == ave::WOULD_BLOCK) {
      return err;
    } else if (err != ave::OK) {
      return err;
    }

    drop = false;
    // TODO(youfa) drop: no need to decode if too late
  } while (drop);

  int64_t timeUs;
  access_unit->meta()->findInt64("timeUs", &timeUs);

  //  AVE_LOG(LS_INFO) << " fetchInputBuffer, pts:" << timeUs;

  message->setBuffer("buffer", std::move(access_unit));
  return ave::OK;
}

bool AvpDecoder::OnInputBufferFetched(
    const std::shared_ptr<MediaPacket>& packet) {
  std::shared_ptr<ave::media::Buffer> buffer;
  bool hasBuffer = message->findBuffer("buffer", buffer);
  if (!buffer.get()) {
    status_t streamErr = ave::media::ERROR_END_OF_STREAM;
    AVE_CHECK(message->findInt32("err", &streamErr) || !hasBuffer);
    AVE_CHECK(streamErr != ave::OK);
  } else {
    status_t err = ave::OK;
    auto codec_buffer = std::make_shared<ave::media::CodecBuffer>();
    // TODO: fill the buffer
    err = decoder_->QueueInputBuffer(codec_buffer);
    if (err != ave::OK) {
      if (err == ave::media::ERROR_RETRY) {
        return false;
      }
      AVE_LOG(LS_ERROR) << "onInputBufferFetched: queueInputBuffer failed:"
                        << err;
      HandleError(err);
    }
  }
  return true;
}

bool AvpDecoder::DoRequestInputBuffers() {
  status_t err = ave::OK;

  while (err == ave::OK && !IsSated()) {
    auto message = std::make_shared<Message>();
    err = FetchInputBuffer(message);

    if (err != ave::OK && err != ave::media::ERROR_END_OF_STREAM) {
      break;
    }

    if (!input_packet_queue_.empty() || !OnInputBufferFetched(message)) {
      input_packet_queue_.push_back(std::move(message));
    }
  }

  return err == ave::WOULD_BLOCK;
}
void AvpDecoder::OnRequestInputBuffers() {
  if (request_input_buffers_pending_) {
    return;
  }

  if (DoRequestInputBuffers()) {
    request_input_buffers_pending_ = true;

    std::shared_ptr<Message> msg(std::make_shared<Message>(
        kWhatRequestInputBuffers, shared_from_this()));

    // TODO(youfa) use frame rate
    msg->post(10 * 1000LL);
  }
}

void AvpDecoder::HandleAnInputBuffer(size_t index) {
  while (!input_packet_queue_.empty()) {
    if (!OnInputBufferFetched(*input_packet_queue_.begin())) {
      break;
    }
    input_packet_queue_.erase(input_packet_queue_.begin());
  }

  OnRequestInputBuffers();
}

void AvpDecoder::HandleAnOutputBuffer(size_t index) {
  std::shared_ptr<CodecBuffer> buffer;
  status_t err = decoder_->DequeueOutputBuffer(buffer, 10 * 1000LL);
  if (buffer.get() == nullptr) {
    if (err != ave::TIMED_OUT) {
      HandleError(err);
      return;
    }
    return;
  }

  auto renderMsg =
      std::make_shared<Message>(kWhatRenderBuffer, shared_from_this());
  renderMsg->setBuffer("buffer", buffer);

  mRender->queueBuffer(is_audio_, buffer, renderMsg);
}

void AvpDecoder::OnInputBufferAvailable(size_t index) {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatInputBufferAvailable,
                                       shared_from_this());
  msg->setInt32("index", index);
  msg->post();
}

void AvpDecoder::OnOutputBufferAvailable(size_t index) {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatOutputBufferAvailable,
                                       shared_from_this());
  msg->setInt32("index", index);
  msg->post();
}

void AvpDecoder::OnOutputFormatChanged(const std::shared_ptr<Message>& format) {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatDecodingFormatChange,
                                       shared_from_this());
  msg->setMessage("message", std::move(format));
  msg->post();
}

void AvpDecoder::OnError(status_t err) {
  auto msg = std::make_shared<Message>(AvpDecoder::kWhatDecodingError,
                                       shared_from_this());
  msg->setInt32("err", err);
  msg->post();
}

void AvpDecoder::onMessageReceived(const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatConfigure: {
      std::shared_ptr<Message> format;
      AVE_CHECK(msg->findMessage("format", format));
      OnConfigure(format);
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
      request_input_buffers_pending_ = false;
      OnRequestInputBuffers();
      break;
    }
    case kWhatStart: {
      OnStart();
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
      int32_t index = 0;
      AVE_CHECK(msg->findInt32("index", &index));
      HandleAnInputBuffer(index);
      break;
    }
    case kWhatOutputBufferAvailable: {
      int32_t index = 0;
      AVE_CHECK(msg->findInt32("index", &index));
      HandleAnOutputBuffer(index);
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

}  // namespace player
}  // namespace ave
