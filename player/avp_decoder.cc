/*
 * avp_decoder.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avp_decoder.h"
#include <unistd.h>

#include <strings.h>
#include <cstring>
#include <memory>

#include "base/checks.h"
#include "base/logging.h"
#include "media/codec/codec_id.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_format.h"
#include "media/foundation/message.h"
#include "media/foundation/message_object.h"

#include "message_def.h"

namespace ave {
namespace player {

using ave::media::CodecId;
using ave::media::MessageObject;

AVPDecoder::AVPDecoder(std::shared_ptr<CodecFactory> codec_factory,
                       std::shared_ptr<Message> notify,
                       std::shared_ptr<ContentSource> source,
                       std::shared_ptr<AVPRender> render)
    : AVPDecoderBase(notify, source, render),
      codec_factory_(std::move(codec_factory)),
      is_audio_(false) {}

AVPDecoder::~AVPDecoder() {
  if (decoder_) {
    decoder_->Stop();
    decoder_->Release();
  }
}

void AVPDecoder::OnConfigure(const std::shared_ptr<MediaFormat>& format) {
  AVE_CHECK(format != nullptr);
  AVE_CHECK(decoder_ == nullptr);

  auto mime = format->mime();

  AVE_LOG(LS_INFO) << "onConfigure, mime:" << mime;

  is_audio_ = !strncasecmp("audio/", mime.c_str(), 6);
  auto codec_id = ave::media::MimeToCodecId(mime.c_str());

  if (codec_id == CodecId::AVE_CODEC_ID_NONE) {
    AVE_LOG(LS_ERROR) << "unknown codec, mime:" << mime;
    ReportError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  decoder_ = codec_factory_->CreateCodecByType(codec_id, false);

  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "decoder create failed, mime:" << mime;
    ReportError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  auto config = std::make_shared<ave::media::CodecConfig>();
  config->format = format;
  config->info.mime = mime;
  config->info.media_type = is_audio_ ? MediaType::AUDIO : MediaType::VIDEO;
  if (video_render_) {
    config->video_render = video_render_;
  }

  status_t err = decoder_->Configure(config);

  if (err != ave::OK) {
    decoder_.reset();
    ReportError(err);
    return;
  }

  decoder_->SetCallback(this);
}

void AVPDecoder::OnSetParameters(const std::shared_ptr<Message>& params) {
  if (decoder_) {
    // TODO: Implement parameter setting for codec
    AVE_LOG(LS_VERBOSE) << "OnSetParameters: " << params->what();
  }
}

void AVPDecoder::OnSetVideoRender(
    const std::shared_ptr<VideoRender>& video_render) {
  video_render_ = video_render;
  if (decoder_ && !is_audio_) {
    // decoder already configured, set video render
    // TODO: config video render on the fly, not support now
    // decoder_->SetVideoRender(video_render);
  }
}

void AVPDecoder::OnStart() {
  AVE_LOG(LS_VERBOSE) << "onStart";
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "Failed to start decoder, no support decoder";
    ReportError(ave::UNKNOWN_ERROR);
    return;
  }

  status_t err = decoder_->Start();

  if (err != ave::OK) {
    AVE_LOG(LS_ERROR) << "Failed to start decoder, err:" << err;
    decoder_.reset();
    ReportError(err);
    return;
  }

  OnRequestInputBuffers();
}

void AVPDecoder::OnPause() {
  AVE_LOG(LS_VERBOSE) << "onPause";
  if (decoder_) {
    decoder_->Stop();
  }
}

void AVPDecoder::OnResume() {
  AVE_LOG(LS_VERBOSE) << "onResume";
  if (decoder_) {
    status_t err = decoder_->Start();
    if (err != ave::OK) {
      ReportError(err);
      return;
    }
    OnRequestInputBuffers();
  }
}

void AVPDecoder::OnFlush() {
  AVE_LOG(LS_VERBOSE) << "onFlush";
  if (decoder_) {
    decoder_->Flush();
  }
  input_packet_queue_.clear();
}

void AVPDecoder::OnShutdown() {
  AVE_LOG(LS_VERBOSE) << "onShutdown";
  if (decoder_) {
    decoder_->Stop();
    decoder_->Release();
    decoder_.reset();
  }
  input_packet_queue_.clear();
}

/////////////////////

bool AVPDecoder::DoRequestInputBuffers() {
  status_t err = OK;
  while (err == OK) {
    std::shared_ptr<MediaPacket> packet;
    err = source_->DequeueAccessUnit(
        is_audio_ ? MediaType::AUDIO : MediaType::VIDEO, packet);
    if (err == WOULD_BLOCK) {
      break;
    }

    if (err != OK) {
      if (err == media::ERROR_END_OF_STREAM) {
        // TODO: send EOS
        AVE_LOG(LS_INFO) << "End of stream reached";
      } else {
        ReportError(err);
      }
      break;
    }
    input_packet_queue_.push_back(packet);
  }
  return (err == WOULD_BLOCK) && (source_->FeedMoreESData() == OK);
}

void AVPDecoder::FillCodecBuffer(std::shared_ptr<CodecBuffer>& buffer) {
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
void AVPDecoder::HandleAnInputBuffer(size_t index) {
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "HandleAnInputBuffer: decoder is nullptr";
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
    AVE_LOG(LS_ERROR) << "QueueInputBuffer failed: " << err;
    ReportError(err);
    return;
  }

  OnRequestInputBuffers();
}

void AVPDecoder::HandleAnOutputBuffer(size_t index) {
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "HandleAnOutputBuffer: decoder is nullptr";
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

  std::shared_ptr<media::MediaFrame> frame;
  if (is_audio_) {
    frame = std::make_shared<media::MediaFrame>(
        media::MediaFrame::Create(buffer->size()));
    frame->SetData(const_cast<uint8_t*>(buffer->data()), buffer->size());
  } else {
    // For video, if the buffer is a texture or handle, still notify render for
    // AV sync.
    frame = std::make_shared<media::MediaFrame>(media::MediaFrame::Create(0));
  }
  // TODO: Set PTS from buffer metadata when available

  if (avp_render_) {
    // Release the codec buffer after the frame has been rendered or dropped
    auto buffer_to_release = buffer;
    avp_render_->RenderFrame(frame, [this, buffer_to_release](bool rendered) {
      if (decoder_) {
        auto buf = buffer_to_release;  // copy to mutable for API
        decoder_->ReleaseOutputBuffer(buf, rendered);
      }
    });
  } else {
    // If no renderer, just release the buffer
    decoder_->ReleaseOutputBuffer(buffer, false);
  }
}

void AVPDecoder::HandleAnOutputFormatChanged(
    const std::shared_ptr<MediaFormat>& format) {
  if (is_audio_) {
    auto notify = notify_->dup();
    notify->setInt32(kWhat, kWhatAudioOutputFormatChanged);
    // TODO: Fix MediaFormat inheritance issue
    // notify->setObject(kMediaFormat,
    // std::dynamic_pointer_cast<MessageObject>(format));
    notify->post();
  } else {
    auto notify = notify_->dup();
    notify->setInt32(kWhat, kWhatVideoSizeChanged);
    // TODO: Fix MediaFormat inheritance issue
    // notify->setObject(kMediaFormat,
    // std::dynamic_pointer_cast<MessageObject>(format));
    notify->post();
  }
}

void AVPDecoder::HandleAnCodecError(status_t err) {
  ReportError(err);
}

/*********************** CodecCallback *************************/
void AVPDecoder::OnInputBufferAvailable(size_t index) {
  auto msg = std::make_shared<Message>(AVPDecoder::kWhatInputBufferAvailable,
                                       shared_from_this());
  msg->setInt32(kIndex, static_cast<int32_t>(index));
  msg->post();
}

void AVPDecoder::OnOutputBufferAvailable(size_t index) {
  auto msg = std::make_shared<Message>(AVPDecoder::kWhatOutputBufferAvailable,
                                       shared_from_this());
  msg->setInt32(kIndex, static_cast<int32_t>(index));
  msg->post();
}

void AVPDecoder::OnOutputFormatChanged(
    const std::shared_ptr<MediaFormat>& format) {
  auto msg = std::make_shared<Message>(AVPDecoder::kWhatDecodingFormatChange,
                                       shared_from_this());
  msg->setObject(kMediaFormat,
                 std::dynamic_pointer_cast<MessageObject>(format));
  msg->post();
}

void AVPDecoder::OnError(status_t err) {
  auto msg = std::make_shared<Message>(AVPDecoder::kWhatDecodingError,
                                       shared_from_this());
  msg->setInt32(kError, err);
  msg->post();
}

void AVPDecoder::OnFrameRendered(std::shared_ptr<Message> notify) {
  auto msg = std::make_shared<Message>(AVPDecoder::kWhatFrameRendered,
                                       shared_from_this());
  msg->post();
}

void AVPDecoder::onMessageReceived(const std::shared_ptr<Message>& msg) {
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
      AVE_CHECK(msg->findObject(kMediaFormat, format));
      HandleAnOutputFormatChanged(
          std::dynamic_pointer_cast<MediaFormat>(format));
      break;
    }
    case kWhatDecodingError: {
      status_t err = 0;
      AVE_CHECK(msg->findInt32(kError, &err));
      HandleAnCodecError(err);
      break;
    }

    case kWhatFrameRendered: {
      // No longer used: release is handled in render callback.
      break;
    }

    default:
      AVPDecoderBase::onMessageReceived(msg);
      break;
  }
}

}  // namespace player
}  // namespace ave
