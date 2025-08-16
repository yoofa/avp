/*
 * avp_tunnel_decoder.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_tunnel_decoder.h"

#include <cstring>
#include <memory>

#include "base/checks.h"
#include "base/logging.h"
#include "media/codec/codec_id.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/message_object.h"

#include "message_def.h"

namespace ave {
namespace player {

using ave::media::CodecId;
using ave::media::MessageObject;

AVPTunnelDecoder::AVPTunnelDecoder(std::shared_ptr<CodecFactory> codec_factory,
                                   std::shared_ptr<Message> notify,
                                   std::shared_ptr<ContentSource> source,
                                   std::shared_ptr<VideoRender> video_render)
    : AVPDecoderBase(notify, source, nullptr),
      codec_factory_(std::move(codec_factory)),
      video_render_(std::move(video_render)) {}

AVPTunnelDecoder::~AVPTunnelDecoder() {
  if (decoder_) {
    decoder_->Stop();
    decoder_->Release();
  }
}

void AVPTunnelDecoder::OnConfigure(const std::shared_ptr<MediaMeta>& format) {
  AVE_CHECK(format != nullptr);
  AVE_CHECK(decoder_ == nullptr);

  auto mime = format->mime();
  AVE_LOG(LS_INFO) << "AVPTunnelDecoder::onConfigure, mime:" << mime;

  auto codec_id = ave::media::MimeToCodecId(mime.c_str());
  if (codec_id == CodecId::AVE_CODEC_ID_NONE) {
    AVE_LOG(LS_ERROR) << "unknown codec, mime:" << mime;
    ReportError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  decoder_ = codec_factory_->CreateCodecByType(codec_id, false);
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "tunnel decoder create failed, mime:" << mime;
    ReportError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  auto config = std::make_shared<ave::media::CodecConfig>();
  config->format = format;
  config->info.mime = mime;
  config->info.media_type = MediaType::VIDEO;
  config->video_render = video_render_;

  status_t err = decoder_->Configure(config);
  if (err != ave::OK) {
    decoder_.reset();
    ReportError(err);
    return;
  }

  decoder_->SetCallback(this);
}

void AVPTunnelDecoder::OnSetParameters(const std::shared_ptr<Message>& params) {
  if (decoder_) {
    // TODO: Implement parameter setting for tunnel codec
    AVE_LOG(LS_VERBOSE) << "AVPTunnelDecoder::OnSetParameters: "
                        << params->what();
  }
}

void AVPTunnelDecoder::OnSetVideoRender(
    const std::shared_ptr<VideoRender>& video_render) {
  video_render_ = video_render;
  if (decoder_) {
    auto config = std::make_shared<ave::media::CodecConfig>();
    config->video_render = video_render_;
    decoder_->Configure(config);
  }
}

void AVPTunnelDecoder::OnStart() {
  AVE_LOG(LS_VERBOSE) << "AVPTunnelDecoder::onStart";
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "Failed to start tunnel decoder, no support decoder";
    ReportError(ave::UNKNOWN_ERROR);
    return;
  }

  status_t err = decoder_->Start();
  if (err != ave::OK) {
    AVE_LOG(LS_ERROR) << "Failed to start tunnel decoder, err:" << err;
    decoder_.reset();
    ReportError(err);
    return;
  }

  OnRequestInputBuffers();
}

void AVPTunnelDecoder::OnPause() {
  AVE_LOG(LS_VERBOSE) << "AVPTunnelDecoder::onPause";
  if (decoder_) {
    decoder_->Stop();
  }
}

void AVPTunnelDecoder::OnResume() {
  AVE_LOG(LS_VERBOSE) << "AVPTunnelDecoder::onResume";
  if (decoder_) {
    status_t err = decoder_->Start();
    if (err != ave::OK) {
      ReportError(err);
      return;
    }
    onRequestInputBuffers();
  }
}

void AVPTunnelDecoder::OnFlush() {
  AVE_LOG(LS_VERBOSE) << "AVPTunnelDecoder::onFlush";
  if (decoder_) {
    decoder_->Flush();
  }
  input_packet_queue_.clear();
}

void AVPTunnelDecoder::OnShutdown() {
  AVE_LOG(LS_VERBOSE) << "AVPTunnelDecoder::onShutdown";
  if (decoder_) {
    decoder_->Stop();
    decoder_->Release();
    decoder_.reset();
  }
  input_packet_queue_.clear();
}

void AVPTunnelDecoder::onRequestInputBuffers() {
  DoRequestInputBuffers();
}

bool AVPTunnelDecoder::DoRequestInputBuffers() {
  status_t err = OK;
  while (err == OK) {
    std::shared_ptr<MediaFrame> packet;
    err = source_->DequeueAccessUnit(MediaType::VIDEO, packet);
    if (err == WOULD_BLOCK) {
      break;
    }
    if (err != OK) {
      if (err == media::ERROR_END_OF_STREAM) {
        AVE_LOG(LS_INFO) << "Tunnel decoder: End of stream reached";
      } else {
        ReportError(err);
      }
      break;
    }
    input_packet_queue_.push_back(packet);
  }
  return (err == WOULD_BLOCK) && (source_->FeedMoreESData() == OK);
}

void AVPTunnelDecoder::FillCodecBuffer(std::shared_ptr<CodecBuffer>& buffer) {
  if (input_packet_queue_.empty()) {
    return;
  }
  auto packet = input_packet_queue_.front();
  input_packet_queue_.pop_front();

  // TODO: Set metadata when CodecBuffer supports it
  // buffer->meta()->setInt64("pts", packet->pts());
  // buffer->meta()->setInt32("flags", packet->flags());
  buffer->SetRange(0, packet->size());
  memcpy(buffer->data(), packet->data(), packet->size());
}

/************* CodecCallback event handler *************/
void AVPTunnelDecoder::HandleAnInputBuffer(size_t index) {
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "HandleAnInputBuffer: tunnel decoder is nullptr";
    ReportError(ave::NO_INIT);
    return;
  }

  std::shared_ptr<CodecBuffer> codec_buffer;
  decoder_->GetInputBuffer(index, codec_buffer);
  if (codec_buffer == nullptr) {
    AVE_LOG(LS_ERROR) << "HandleAnInputBuffer: codec_buffer is nullptr";
    ReportError(ave::UNKNOWN_ERROR);
    return;
  }

  FillCodecBuffer(codec_buffer);

  status_t err = decoder_->QueueInputBuffer(codec_buffer);
  if (err != OK) {
    AVE_LOG(LS_ERROR) << "Tunnel QueueInputBuffer failed: " << err;
    ReportError(err);
    return;
  }

  onRequestInputBuffers();
}

void AVPTunnelDecoder::HandleAnOutputBuffer(size_t index) {
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "HandleAnOutputBuffer: tunnel decoder is nullptr";
    ReportError(ave::NO_INIT);
    return;
  }

  std::shared_ptr<CodecBuffer> buffer;
  status_t err = decoder_->GetOutputBuffer(index, buffer);
  if (err != OK) {
    ReportError(err);
    return;
  }

  if (buffer == nullptr) {
    AVE_LOG(LS_ERROR) << "HandleAnOutputBuffer: buffer is nullptr";
    ReportError(ave::UNKNOWN_ERROR);
    return;
  }

  // In tunnel mode, the hardware automatically renders the frame
  // We just need to release the buffer with render=true
  decoder_->ReleaseOutputBuffer(buffer, true);
}

void AVPTunnelDecoder::HandleAnOutputFormatChanged(
    const std::shared_ptr<MediaMeta>& format) {
  auto notify = notify_->dup();
  notify->setInt32(kWhat, kWhatTunnelFormatChanged);
  notify->setObject(kMediaMeta, format);
  notify->post();
}

void AVPTunnelDecoder::HandleAnCodecError(status_t err) {
  ReportError(err);
}

/*********************** CodecCallback *************************/
void AVPTunnelDecoder::OnInputBufferAvailable(size_t index) {
  auto msg = std::make_shared<Message>(
      AVPTunnelDecoder::kWhatInputBufferAvailable, shared_from_this());
  msg->setInt32(kIndex, static_cast<int32_t>(index));
  msg->post();
}

void AVPTunnelDecoder::OnOutputBufferAvailable(size_t index) {
  auto msg = std::make_shared<Message>(
      AVPTunnelDecoder::kWhatOutputBufferAvailable, shared_from_this());
  msg->setInt32(kIndex, static_cast<int32_t>(index));
  msg->post();
}

void AVPTunnelDecoder::OnOutputFormatChanged(
    const std::shared_ptr<MediaMeta>& format) {
  auto msg = std::make_shared<Message>(
      AVPTunnelDecoder::kWhatDecodingFormatChange, shared_from_this());
  msg->setObject(kMediaMeta, std::dynamic_pointer_cast<MessageObject>(format));
  msg->post();
}

void AVPTunnelDecoder::OnError(status_t err) {
  auto msg = std::make_shared<Message>(AVPTunnelDecoder::kWhatDecodingError,
                                       shared_from_this());
  msg->setInt32(kError, err);
  msg->post();
}

void AVPTunnelDecoder::OnFrameRendered(std::shared_ptr<Message> notify) {
  // In tunnel mode, frames are rendered automatically by hardware
  // This callback is typically not used
  notify->post();
}

void AVPTunnelDecoder::onMessageReceived(const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatSetVideoRender: {
      std::shared_ptr<MessageObject> obj;
      AVE_CHECK(msg->findObject(kVideoRender, obj));
      auto sink = std::dynamic_pointer_cast<VideoRender>(obj);
      OnSetVideoRender(sink);
      break;
    }

    // codec notify event
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
      std::shared_ptr<MessageObject> format;
      AVE_CHECK(msg->findObject(kMediaMeta, format));
      HandleAnOutputFormatChanged(std::dynamic_pointer_cast<MediaMeta>(format));
      break;
    }
    case kWhatDecodingError: {
      status_t err = 0;
      AVE_CHECK(msg->findInt32(kError, &err));
      HandleAnCodecError(err);
      break;
    }

    default:
      AVPDecoderBase::onMessageReceived(msg);
      break;
  }
}

}  // namespace player
}  // namespace ave
