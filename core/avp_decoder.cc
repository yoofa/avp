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
#include "media/foundation/media_meta.h"

#include "message_def.h"

namespace ave {
namespace player {

using ave::media::CodecId;

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

void AVPDecoder::OnConfigure(const std::shared_ptr<MediaMeta>& format) {
  AVE_CHECK(format != nullptr);
  AVE_CHECK(decoder_ == nullptr);

  auto mime = format->mime();

  AVE_LOG(LS_INFO) << "AVPDecoder::OnConfigure: mime=" << mime
                   << ", stream_type="
                   << static_cast<int>(format->stream_type());

  is_audio_ = !strncasecmp("audio/", mime.c_str(), 6);
  auto codec_id = ave::media::MimeToCodecId(mime.c_str());

  if (codec_id == CodecId::AVE_CODEC_ID_NONE) {
    AVE_LOG(LS_ERROR) << "AVPDecoder::OnConfigure: unknown codec, mime="
                      << mime;
    ReportError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  AVE_LOG(LS_INFO) << "AVPDecoder::OnConfigure: codec_id="
                   << static_cast<int>(codec_id)
                   << ", creating decoder via factory";
  decoder_ = codec_factory_->CreateCodecByType(codec_id, false);

  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "AVPDecoder::OnConfigure: decoder create failed"
                      << ", mime=" << mime;
    ReportError(ave::media::ERROR_UNSUPPORTED);
    return;
  }

  AVE_LOG(LS_INFO) << "AVPDecoder::OnConfigure: codec created, configuring...";

  auto config = std::make_shared<ave::media::CodecConfig>();
  config->format = format;
  config->info.mime = mime;
  config->info.media_type = is_audio_ ? MediaType::AUDIO : MediaType::VIDEO;
  if (video_render_) {
    config->video_render = video_render_;
    AVE_LOG(LS_INFO) << "AVPDecoder::OnConfigure: video_render set: "
                     << video_render_.get();
  } else if (!is_audio_) {
    AVE_LOG(LS_WARNING)
        << "AVPDecoder::OnConfigure: video decoder has NO video_render set!";
  }

  status_t err = decoder_->Configure(config);

  if (err != ave::OK) {
    AVE_LOG(LS_ERROR) << "AVPDecoder::OnConfigure: Configure failed, err="
                      << err;
    decoder_.reset();
    ReportError(err);
    return;
  }

  AVE_LOG(LS_INFO) << "AVPDecoder::OnConfigure: setting callback";
  decoder_->SetCallback(this);
  AVE_LOG(LS_INFO) << "AVPDecoder::OnConfigure: complete";
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
  AVE_LOG(LS_INFO) << "AVPDecoder::OnStart: is_audio=" << is_audio_;
  if (decoder_ == nullptr) {
    AVE_LOG(LS_ERROR) << "AVPDecoder::OnStart: no decoder";
    ReportError(ave::UNKNOWN_ERROR);
    return;
  }

  status_t err = decoder_->Start();

  if (err != ave::OK) {
    AVE_LOG(LS_ERROR) << "AVPDecoder::OnStart: Start failed, err=" << err;
    decoder_.reset();
    ReportError(err);
    return;
  }

  AVE_LOG(LS_INFO) << "AVPDecoder::OnStart: codec started, requesting input";
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
  AVE_LOG(LS_INFO) << "DoRequestInputBuffers: is_audio=" << is_audio_
                   << ", current_queue_size=" << input_packet_queue_.size();
  status_t err = OK;
  while (err == OK) {
    std::shared_ptr<MediaFrame> packet;
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
  bool should_retry = (err == WOULD_BLOCK) && (source_->FeedMoreESData() == OK);
  AVE_LOG(LS_INFO) << "DoRequestInputBuffers done: queued="
                   << input_packet_queue_.size()
                   << ", should_retry=" << should_retry;
  return should_retry;
}

void AVPDecoder::FillCodecBuffer(std::shared_ptr<CodecBuffer>& buffer) {
  if (input_packet_queue_.empty()) {
    return;
  }
  auto packet = input_packet_queue_.front();
  input_packet_queue_.pop_front();

  buffer->SetRange(0, packet->size());
  memcpy(buffer->data(), packet->data(), packet->size());

  // Pass the PTS from input packet so the codec can stamp output frames
  auto meta = media::MediaMeta::CreatePtr(
      is_audio_ ? MediaType::AUDIO : MediaType::VIDEO,
      media::MediaMeta::FormatType::kSample);
  auto pkt_pts = packet->pts();
  meta->SetPts(pkt_pts);
  buffer->format() = meta;
}

/************* CodecCallback event handler *************/
void AVPDecoder::HandleAnInputBuffer(size_t index) {
  AVE_LOG(LS_INFO) << "HandleAnInputBuffer: index=" << index
                   << ", queue_size=" << input_packet_queue_.size();
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

  // Refill packet queue before filling the codec buffer
  OnRequestInputBuffers();

  if (input_packet_queue_.empty()) {
    // No data yet; schedule a retry so we don't send an empty (EOS) buffer
    auto retry_msg =
        std::make_shared<Message>(kWhatRetryInputBuffer, shared_from_this());
    retry_msg->setInt32(kIndex, static_cast<int32_t>(index));
    retry_msg->post(5 * 1000LL);  // 5 ms
    return;
  }

  FillCodecBuffer(codec_buffer);

  AVE_LOG(LS_INFO) << "HandleAnInputBuffer: queuing buffer index=" << index
                   << ", size=" << codec_buffer->size();
  status_t err = decoder_->QueueInputBuffer(index);
  if (err != OK) {
    AVE_LOG(LS_ERROR) << "QueueInputBuffer failed: " << err;
    ReportError(err);
    return;
  }
}

void AVPDecoder::HandleAnOutputBuffer(size_t index) {
  AVE_LOG(LS_INFO) << "HandleAnOutputBuffer: index=" << index;
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
  // Surface mode: codec renders directly to ANativeWindow; buffer has no data.
  bool is_surface_mode =
      !is_audio_ && (buffer->data() == nullptr || buffer->size() == 0);
  bool already_released = false;

  if (is_audio_) {
    frame = media::MediaFrame::CreateSharedAsCopy(
        buffer->data(), buffer->size(), MediaType::AUDIO);
    // Copy audio format metadata (sample rate, channels, PTS, etc.)
    if (buffer->format() && buffer->format()->sample_info()) {
      *frame->audio_info() = buffer->format()->sample_info()->audio();
    }
  } else if (is_surface_mode) {
    // Surface mode: create a metadata-only frame for AV sync timing.
    // The actual rendering is done by ReleaseOutputBuffer(index, true).
    frame = media::MediaFrame::CreateShared(0, MediaType::VIDEO);
    if (buffer->format() && buffer->format()->sample_info()) {
      const auto& vinfo = buffer->format()->sample_info()->video();
      frame->SetWidth(vinfo.width);
      frame->SetHeight(vinfo.height);
      if (!vinfo.pts.IsMinusInfinity()) {
        frame->SetPts(vinfo.pts);
      }
    }
    AVE_LOG(LS_INFO) << "HandleAnOutputBuffer: surface mode video frame, pts="
                     << (frame ? frame->pts().us_or(-1) : -1);
  } else {
    // Buffer mode: copy the YUV pixel data from the codec buffer, then
    // immediately release the codec buffer so the OutputThread is unblocked.
    frame = media::MediaFrame::CreateSharedAsCopy(
        buffer->data(), buffer->size(), MediaType::VIDEO);
    if (buffer->format() && buffer->format()->sample_info()) {
      const auto& vinfo = buffer->format()->sample_info()->video();
      frame->SetWidth(vinfo.width);
      frame->SetHeight(vinfo.height);
      frame->SetStride(vinfo.stride > 0 ? vinfo.stride : vinfo.width);
      frame->SetPixelFormat(vinfo.pixel_format);
      if (!vinfo.pts.IsMinusInfinity()) {
        frame->SetPts(vinfo.pts);
      }
    }
    // Data is copied into the frame — release the codec buffer immediately.
    decoder_->ReleaseOutputBuffer(index, false);
    already_released = true;
    AVE_LOG(LS_INFO) << "HandleAnOutputBuffer: buffer mode video frame, pts="
                     << (frame ? frame->pts().us_or(-1) : -1)
                     << ", size=" << (frame ? frame->size() : 0);
  }

  if (avp_render_) {
    auto decoder = decoder_;
    avp_render_->RenderFrame(frame, [decoder, index, is_surface_mode,
                                     already_released](bool rendered) {
      if (!already_released) {
        // Surface mode: render=true causes the codec to blit the decoded
        // frame to the ANativeWindow at this exact moment.
        decoder->ReleaseOutputBuffer(index, is_surface_mode && rendered);
      }
      // Buffer mode: already released immediately after data copy above.
    });
    AVE_LOG(LS_INFO) << "HandleAnOutputBuffer: queuing frame to render, "
                     << "surface_mode=" << is_surface_mode
                     << ", pts=" << (frame ? frame->pts().us_or(-1) : -1);
  } else {
    if (!already_released) {
      decoder_->ReleaseOutputBuffer(index, false);
    }
  }
}

void AVPDecoder::HandleAnOutputFormatChanged(
    const std::shared_ptr<MediaMeta>& format) {
  if (is_audio_) {
    auto notify = notify_->dup();
    notify->setInt32(kWhat, kWhatAudioOutputFormatChanged);
    // TODO: Fix MediaMeta inheritance issue
    notify->post();
  } else {
    auto notify = notify_->dup();
    notify->setInt32(kWhat, kWhatVideoSizeChanged);
    // TODO: Fix MediaMeta inheritance issue
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
    const std::shared_ptr<MediaMeta>& format) {
  auto msg = std::make_shared<Message>(AVPDecoder::kWhatDecodingFormatChange,
                                       shared_from_this());
  msg->setObject(kMediaMeta, format);
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
      std::shared_ptr<VideoRender> video_render;
      AVE_CHECK(msg->findObject(kVideoRender, video_render));
      OnSetVideoRender(video_render);
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
      std::shared_ptr<MediaMeta> format;
      AVE_CHECK(msg->findObject(kMediaMeta, format));
      HandleAnOutputFormatChanged(format);
      break;
    }
    case kWhatDecodingError: {
      status_t err = 0;
      AVE_CHECK(msg->findInt32(kError, &err));
      HandleAnCodecError(err);
      break;
    }

    case kWhatRetryInputBuffer: {
      int32_t index = 0;
      AVE_CHECK(msg->findInt32(kIndex, &index));
      HandleAnInputBuffer(static_cast<size_t>(index));
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
