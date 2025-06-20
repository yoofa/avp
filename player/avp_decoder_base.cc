/*
 * avp_decoder_base.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avp_decoder_base.h"

#include <unistd.h>
#include <memory>

#include "base/checks.h"
#include "media/foundation/message_object.h"

#include "message_def.h"

using ::ave::media::MessageObject;

namespace ave {
namespace player {

AVPDecoderBase::AVPDecoderBase(
    std::shared_ptr<Message> notify,
    std::shared_ptr<ContentSource> source,
    std::shared_ptr<AVSynchronizeRender> synchronizer)
    : notify_(std::move(notify)),
      source_(std::move(source)),
      synchronizer_(std::move(synchronizer)),
      looper_(std::make_shared<Looper>()),
      request_input_buffers_pending_(false) {
  looper_->setName("AVPDecoder");
}

AVPDecoderBase::~AVPDecoderBase() {
  looper_->unregisterHandler(id());
  looper_->stop();
}

void AVPDecoderBase::Init() {
  looper_->start();
  looper_->registerHandler(shared_from_this());
}

void AVPDecoderBase::Configure(const std::shared_ptr<MediaFormat>& format) {
  auto msg(std::make_shared<Message>(kWhatConfigure, shared_from_this()));
  msg->setObject(kMediaFormat, std::static_pointer_cast<MessageObject>(format));
  msg->post();
}

// set parameters on the fly
void AVPDecoderBase::SetParameters(const std::shared_ptr<Message>& parameters) {
  auto msg(std::make_shared<Message>(kWhatSetParameters, shared_from_this()));
  msg->setMessage("format", parameters);
  msg->post();
}
void AVPDecoderBase::SetSynchronizer(
    const std::shared_ptr<AVSynchronizeRender> synchronizer) {
  auto msg(std::make_shared<Message>(kWhatSetSynchronizer, shared_from_this()));
  msg->setObject(kSynchronizer, std::move(synchronizer));
  msg->post();
}
void AVPDecoderBase::SetVideoRender(
    const std::shared_ptr<VideoRender> video_render) {
  auto msg(std::make_shared<Message>(kWhatSetVideoRender, shared_from_this()));

  msg->setObject(kVideoRender, std::move(video_render));
  msg->post();
}

void AVPDecoderBase::Start() {
  auto msg(std::make_shared<Message>(kWhatStart, shared_from_this()));
  msg->post();
}

void AVPDecoderBase::Pause() {
  auto msg(std::make_shared<Message>(kWhatPause, shared_from_this()));
  std::shared_ptr<Message> response;
  msg->postAndWaitResponse(response);
}

void AVPDecoderBase::Resume() {
  auto msg(std::make_shared<Message>(kWhatResume, shared_from_this()));
  msg->post();
}

void AVPDecoderBase::Flush() {
  auto msg(std::make_shared<Message>(kWhatFlush, shared_from_this()));
  msg->post();
}

void AVPDecoderBase::Shutdown() {
  auto msg(std::make_shared<Message>(kWhatShutdown, shared_from_this()));
  msg->post();
}

void AVPDecoderBase::ReportError(status_t err) {
  auto notify = notify_->dup();
  notify->setInt32(kWhat, kWhatDecoderError);
  notify->setInt32(kError, err);
  notify->post();
}

//////////////////////////////////////////

void AVPDecoderBase::OnRequestInputBuffers() {
  if (request_input_buffers_pending_) {
    return;
  }

  if (DoRequestInputBuffers()) {
    request_input_buffers_pending_ = true;

    auto msg(std::make_shared<Message>(kWhatRequestInputBuffers,
                                       shared_from_this()));

    // TODO(youfa) rate control
    msg->post(10 * 1000LL);
  }
}

void AVPDecoderBase::onMessageReceived(const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatConfigure: {
      std::shared_ptr<MessageObject> format;
      AVE_CHECK(msg->findObject(kMediaFormat, format));
      OnConfigure(std::dynamic_pointer_cast<MediaFormat>(format));
      break;
    }
    case kWhatSetParameters: {
      std::shared_ptr<Message> parameters;
      AVE_CHECK(msg->findMessage(kParameters, parameters));
      OnSetParameters(parameters);
      break;
    }
    case kWhatSetSynchronizer: {
      std::shared_ptr<MessageObject> synchronizer;
      AVE_CHECK(msg->findObject(kSynchronizer, synchronizer));
      OnSetSynchronizer(
          std::dynamic_pointer_cast<AVSynchronizeRender>(synchronizer));
      break;
    }
    case kWhatSetVideoRender: {
      std::shared_ptr<MessageObject> render;
      AVE_CHECK(msg->findObject(kVideoRender, render));
      OnSetVideoRender(std::dynamic_pointer_cast<VideoRender>(render));
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
      OnPause();
      break;
    }

    case kWhatResume: {
      OnResume();
      break;
    }
    case kWhatFlush: {
      OnFlush();
      break;
    }
    case kWhatShutdown: {
      OnShutdown();
      break;
    }

    default: {
      break;
    }
  }
}

}  // namespace player
}  // namespace ave
