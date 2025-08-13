/*
 * avp_video_render.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_video_render.h"

#include "base/logging.h"
#include "media/foundation/media_meta.h"

namespace ave {
namespace player {

AVPVideoRender::AVPVideoRender(base::TaskRunnerFactory* task_runner_factory,
                               IAVSyncController* avsync_controller)
    : AVPRender(task_runner_factory, avsync_controller),
      current_video_format_(media::MediaType::VIDEO,
                            media::MediaMeta::FormatType::kSample),
      format_initialized_(false),
      total_frames_rendered_(0),
      total_frames_dropped_(0),
      last_video_pts_us_(0) {
  AVE_LOG(LS_INFO) << "AVPVideoRender created";
}

AVPVideoRender::~AVPVideoRender() {
  AVE_LOG(LS_INFO) << "AVPVideoRender destroyed";
}

void AVPVideoRender::SetSink(std::shared_ptr<media::VideoRender> video_render) {
  std::lock_guard<std::mutex> lock(mutex_);
  video_render_ = video_render;
  AVE_LOG(LS_INFO) << "Video sink set: " << (video_render_ ? "valid" : "null");
}

void AVPVideoRender::Start() {
  AVPRender::Start();
  AVE_LOG(LS_INFO) << "AVPVideoRender started";
}

void AVPVideoRender::Stop() {
  AVPRender::Stop();
  AVE_LOG(LS_INFO) << "AVPVideoRender stopped";
}

void AVPVideoRender::Pause() {
  AVPRender::Pause();
  AVE_LOG(LS_INFO) << "AVPVideoRender paused";
}

void AVPVideoRender::Resume() {
  AVPRender::Resume();
  AVE_LOG(LS_INFO) << "AVPVideoRender resumed";
}

void AVPVideoRender::Flush() {
  AVPRender::Flush();
  AVE_LOG(LS_INFO) << "AVPVideoRender flushed";
}

uint64_t AVPVideoRender::RenderFrameInternal(
    std::shared_ptr<media::MediaFrame>& frame) {
  if (!frame || frame->GetMediaType() != media::MediaType::VIDEO) {
    AVE_LOG(LS_WARNING) << "Invalid video frame";
    return 0;
  }

  if (!video_render_) {
    AVE_LOG(LS_WARNING) << "No video sink available, dropping frame";
    total_frames_dropped_++;
    return 0;
  }

  // Update format tracking
  if (!format_initialized_) {
    auto* video_info = frame->video_info();
    if (video_info) {
      current_video_format_.SetWidth(video_info->width);
      current_video_format_.SetHeight(video_info->height);
      current_video_format_.SetPixelFormat(video_info->pixel_format);
      format_initialized_ = true;
      AVE_LOG(LS_INFO) << "Video format initialized: " << video_info->width
                       << "x" << video_info->height;
    }
  }

  // Pass frame to video render sink
  video_render_->OnFrame(frame);
  total_frames_rendered_++;

  // Update last PTS
  auto* video_info = frame->video_info();
  if (video_info && !video_info->pts.IsMinusInfinity()) {
    last_video_pts_us_ = video_info->pts.us();
  }

  AVE_LOG(LS_VERBOSE) << "Video frame rendered, total: "
                      << total_frames_rendered_
                      << ", dropped: " << total_frames_dropped_;

  // Return 0 for immediate rendering (no delay)
  return 0;
}

}  // namespace player
}  // namespace ave
