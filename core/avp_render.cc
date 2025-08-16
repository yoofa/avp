/*
 * avp_render.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_render.h"

#include "base/logging.h"
#include "base/task_util/default_task_runner_factory.h"

namespace ave {
namespace player {

AVPRender::AVPRender(base::TaskRunnerFactory* task_runner_factory,
                     IAVSyncController* avsync_controller)
    : task_runner_(std::make_unique<base::TaskRunner>(
          task_runner_factory->CreateTaskRunner(
              "AVPRender",
              base::TaskRunnerFactory::Priority::NORMAL))),
      avsync_controller_(avsync_controller),
      update_generation_(0),
      running_(false),
      paused_(false) {
  AVE_CHECK(avsync_controller_ != nullptr);
}

AVPRender::~AVPRender() {
  Stop();
}

void AVPRender::RenderFrame(std::shared_ptr<media::MediaFrame> frame,
                            std::unique_ptr<RenderEvent> render_event) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!running_) {
    AVE_LOG(LS_VERBOSE) << "Renderer not running, dropping frame";
    return;
  }

  if (!frame) {
    AVE_LOG(LS_WARNING) << "Received null frame";
    return;
  }

  // Check queue size to prevent memory overflow
  if (frame_queue_.size() >= kMaxQueueSize) {
    AVE_LOG(LS_WARNING) << "Frame queue full, dropping oldest frame";
    frame_queue_.pop();
  }

  // Add frame to queue
  frame_queue_.push(QueueEntry{std::move(frame), std::move(render_event)});

  // Schedule next frame if not already scheduling and not paused
  if (!paused_) {
    ScheduleNextFrame();
  }
}

int64_t AVPRender::GetCurrentTimeStamp() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (avsync_controller_) {
    return avsync_controller_->GetMasterClock();
  }
  return 0;
}

void AVPRender::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) {
    running_ = true;
    paused_ = false;

    // Start processing queued frames
    if (!frame_queue_.empty()) {
      ScheduleNextFrame();
    }
  }
}

void AVPRender::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    running_ = false;
    paused_ = false;
    update_generation_++;

    // Clear the frame queue
    while (!frame_queue_.empty()) {
      frame_queue_.pop();
    }
  }
}

void AVPRender::Pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_ && !paused_) {
    paused_ = true;
    update_generation_++;
  }
}

void AVPRender::Resume() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_ && paused_) {
    paused_ = false;

    // Resume processing queued frames
    if (!frame_queue_.empty()) {
      ScheduleNextFrame();
    }
  }
}

void AVPRender::Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  update_generation_++;

  // Clear the frame queue
  while (!frame_queue_.empty()) {
    frame_queue_.pop();
  }
}

void AVPRender::ScheduleNextFrame(uint32_t delay_us) {
  if (!task_runner_) {
    AVE_LOG(LS_ERROR) << "Task runner not available";
    return;
  }

  if (frame_queue_.empty()) {
    return;
  }

  update_generation_++;
  // Schedule the task with delay
  task_runner_->PostDelayedTask(
      [this, update_generation = update_generation_]() {
        // AVE_DCHECK_RUN_ON(task_runner_.get());  // TODO: Fix for test
        // environment
        OnRenderTask(update_generation);
      },
      delay_us);
}

int64_t AVPRender::CalculateRenderLateUs(
    const std::shared_ptr<media::MediaFrame>& frame) {
  // Calculate render delay based on frame PTS and current timestamp
  int64_t frame_pts_us = 0;
  if (frame->stream_type() == media::MediaType::AUDIO) {
    auto* audio_info = frame->audio_info();
    if (audio_info) {
      frame_pts_us = audio_info->pts.us();
    }
  } else if (frame->stream_type() == media::MediaType::VIDEO) {
    auto* video_info = frame->video_info();
    if (video_info) {
      frame_pts_us = video_info->pts.us();
    }
  }

  int64_t current_timestamp_us = 0;
  if (avsync_controller_) {
    current_timestamp_us = avsync_controller_->GetMasterClock();
  }
  int64_t late_us = current_timestamp_us - frame_pts_us;

  return late_us;
}

void AVPRender::OnRenderTask(int64_t update_generation) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if renderer is still in valid state
  if (!running_ || paused_) {
    AVE_LOG(LS_VERBOSE) << "Renderer not in valid state, dropping frame";
    return;
  }

  if (update_generation != update_generation_) {
    AVE_LOG(LS_VERBOSE) << "Dropping stale render task";
    return;
  }

  if (frame_queue_.empty()) {
    return;
  }

  uint64_t next_render_delay_us = 0;

  auto& entry = frame_queue_.front();
  auto& frame = entry.frame;

  if (frame->stream_type() == media::MediaType::AUDIO) {
    // For audio, render immediately and schedule next based on returned delay

    next_render_delay_us = RenderFrameInternal(frame);
    frame_queue_.pop();
  } else {
    // For video/subtitle, use original logic with drop frame behavior
    // Calculate delay within mutex lock
    int64_t late_us = CalculateRenderLateUs(frame);

    if (late_us > 40000) {
      // too late, drop the frame
      AVE_LOG(LS_INFO) << "Dropping frame, delay: " << late_us << "us";
      ReleaseFrame(entry, false);
      frame_queue_.pop();
    } else if (late_us > -5000) {
      // not too early, render the frame
      try {
        RenderFrameInternal(frame);
        ReleaseFrame(entry, true);
        frame_queue_.pop();
      } catch (const std::exception& e) {
        AVE_LOG(LS_ERROR) << "Exception in RenderFrameInternal: " << e.what();
      }
    } else {
      // too early, schedule next frame
      next_render_delay_us = -late_us;
    }
  }
  // Schedule next frame if queue is not empty
  if (!frame_queue_.empty()) {
    ScheduleNextFrame(next_render_delay_us);  // TODO: Fix for test environment
  }
}

void AVPRender::ReleaseFrame(QueueEntry& entry, bool render) {
  if (entry.render_event) {
    entry.render_event->OnRenderEvent(render);
  }
}

}  // namespace player
}  // namespace ave
