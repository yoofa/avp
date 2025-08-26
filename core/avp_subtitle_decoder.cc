/*
 * avp_subtitle_decoder.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_subtitle_decoder.h"

#include <memory>
#include <sstream>

#include "base/checks.h"
#include "base/logging.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"
#include "media/foundation/message_object.h"

#include "message_def.h"

using ave::media::MediaFrame;

namespace ave {
namespace player {

AVPSubtitleDecoder::AVPSubtitleDecoder(std::shared_ptr<Message> notify,
                                       std::shared_ptr<ContentSource> source,
                                       std::shared_ptr<AVPRender> render)
    : AVPDecoderBase(notify, source, render),
      current_subtitle_start_(-1),
      current_subtitle_end_(-1) {
  AVE_LOG(LS_VERBOSE) << "AVPSubtitleDecoder created";
}

AVPSubtitleDecoder::~AVPSubtitleDecoder() {
  AVE_LOG(LS_VERBOSE) << "~AVPSubtitleDecoder";
}

void AVPSubtitleDecoder::OnConfigure(const std::shared_ptr<MediaMeta>& format) {
  AVE_LOG(LS_VERBOSE) << "OnConfigure";

  subtitle_format_ = format->mime();
  current_subtitle_start_ = -1;
  current_subtitle_end_ = -1;
  current_subtitle_frame_.reset();
  subtitle_cache_.clear();

  OnRequestInputBuffers();
}

void AVPSubtitleDecoder::OnSetParameters(
    const std::shared_ptr<Message>& params) {
  AVE_LOG(LS_VERBOSE) << "OnSetParameters: " << params->what();
  // TODO: Implement parameter setting for subtitle decoder
}

void AVPSubtitleDecoder::OnSetVideoRender(
    const std::shared_ptr<VideoRender>& video_render) {
  // Subtitle decoder doesn't need video render directly
  AVE_LOG(LS_VERBOSE) << "OnSetVideoRender: ignored for subtitle decoder";
}

void AVPSubtitleDecoder::OnStart() {
  AVE_LOG(LS_VERBOSE) << "OnStart";
  OnRequestInputBuffers();
}

void AVPSubtitleDecoder::OnPause() {
  AVE_LOG(LS_VERBOSE) << "OnPause";
  // Subtitle decoder doesn't need special pause handling
}

void AVPSubtitleDecoder::OnResume() {
  AVE_LOG(LS_VERBOSE) << "OnResume";
  OnRequestInputBuffers();
}

void AVPSubtitleDecoder::OnFlush() {
  AVE_LOG(LS_VERBOSE) << "OnFlush";
  subtitle_cache_.clear();
  current_subtitle_start_ = -1;
  current_subtitle_end_ = -1;
  current_subtitle_frame_.reset();

  if (avp_render_) {
    avp_render_->Flush();
  }
}

void AVPSubtitleDecoder::OnShutdown() {
  AVE_LOG(LS_VERBOSE) << "OnShutdown";
  subtitle_cache_.clear();
  current_subtitle_start_ = -1;
  current_subtitle_end_ = -1;
  current_subtitle_frame_.reset();

  if (avp_render_) {
    avp_render_->Flush();
  }
}

bool AVPSubtitleDecoder::DoRequestInputBuffers() {
  status_t err = OK;
  while (err == OK) {
    std::shared_ptr<MediaFrame> packet;
    err = source_->DequeueAccessUnit(MediaType::SUBTITLE, packet);
    if (err == WOULD_BLOCK) {
      break;
    }
    if (err != OK) {
      if (err == media::ERROR_END_OF_STREAM) {
        AVE_LOG(LS_INFO) << "Subtitle decoder: End of stream reached";
      } else {
        ReportError(err);
      }
      break;
    }

    ParseSubtitlePacket(packet);
  }

  return (err == WOULD_BLOCK) && (source_->FeedMoreESData() == OK);
}

void AVPSubtitleDecoder::ParseSubtitlePacket(
    const std::shared_ptr<MediaFrame>& packet) {
  if (!packet || packet->size() == 0) {
    return;
  }

  AVE_LOG(LS_VERBOSE) << "ParseSubtitlePacket: format=" << subtitle_format_
                      << ", size=" << packet->size();

  if (subtitle_format_.find("srt") != std::string::npos) {
    ParseSRTSubtitle(packet);
  } else if (subtitle_format_.find("ass") != std::string::npos ||
             subtitle_format_.find("ssa") != std::string::npos) {
    ParseASSSubtitle(packet);
  } else if (subtitle_format_.find("vtt") != std::string::npos) {
    ParseVTTSubtitle(packet);
  } else {
    // Generic subtitle parsing
    auto frame = media::MediaFrame::CreateSharedAsCopy(
        packet->data(), packet->size(), MediaType::SUBTITLE);
    // TODO: Set PTS and duration when MediaFrame supports it

    // TODO: Fix MediaFrame interface
    // subtitle_cache_[packet->pts()] = frame;
    RenderSubtitleFrame(frame);
  }
}

void AVPSubtitleDecoder::ParseSRTSubtitle(
    const std::shared_ptr<MediaFrame>& packet) {
  // Simple SRT parsing - in real implementation, you'd want a more robust
  // parser
  std::string subtitle_text(reinterpret_cast<const char*>(packet->data()),
                            packet->size());

  // Create a subtitle frame
  auto frame = media::MediaFrame::CreateSharedAsCopy(
      packet->data(), packet->size(), MediaType::SUBTITLE);
  // TODO: Set PTS and duration when MediaFrame supports it
  // TODO: Add subtitle metadata when MediaFrame supports it

  subtitle_cache_[packet->meta()->pts().us()] = frame;
  RenderSubtitleFrame(frame);
}

void AVPSubtitleDecoder::ParseASSSubtitle(
    const std::shared_ptr<MediaFrame>& packet) {
  // ASS/SSA subtitle parsing
  std::string subtitle_text(reinterpret_cast<const char*>(packet->data()),
                            packet->size());

  auto frame = media::MediaFrame::CreateSharedAsCopy(
      packet->data(), packet->size(), MediaType::SUBTITLE);
  // TODO: Set PTS and duration when MediaFrame supports it
  // TODO: Add subtitle metadata when MediaFrame supports it

  subtitle_cache_[packet->meta()->pts().us()] = frame;
  RenderSubtitleFrame(frame);
}

void AVPSubtitleDecoder::ParseVTTSubtitle(
    const std::shared_ptr<MediaFrame>& packet) {
  // WebVTT subtitle parsing
  std::string subtitle_text(reinterpret_cast<const char*>(packet->data()),
                            packet->size());

  auto frame = media::MediaFrame::CreateSharedAsCopy(
      packet->data(), packet->size(), MediaType::SUBTITLE);
  // TODO: Set PTS and duration when MediaFrame supports it
  // TODO: Add subtitle metadata when MediaFrame supports it

  subtitle_cache_[packet->meta()->pts().us()] = frame;
  RenderSubtitleFrame(frame);
}

void AVPSubtitleDecoder::RenderSubtitleFrame(
    const std::shared_ptr<media::MediaFrame>& frame) {
  if (!avp_render_) {
    return;
  }

  // Send subtitle frame to renderer for AV sync
  avp_render_->RenderFrame(frame);

  AVE_LOG(LS_VERBOSE) << "Rendered subtitle frame";
  // TODO: Log PTS and duration when MediaFrame supports it
}

void AVPSubtitleDecoder::onMessageReceived(
    const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatParseSubtitle: {
      std::shared_ptr<ave::media::MessageObject> obj;
      if (msg->findObject("packet", obj)) {
        auto packet = std::dynamic_pointer_cast<MediaFrame>(obj);
        if (packet) {
          ParseSubtitlePacket(packet);
        }
      }
      break;
    }

    default:
      AVPDecoderBase::onMessageReceived(msg);
      break;
  }
}

}  // namespace player
}  // namespace ave
