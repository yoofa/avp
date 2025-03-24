/*
 * avp_render.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_AVP_AVP_RENDER_H_H_
#define AVE_AVP_AVP_RENDER_H_H_

#include <memory>
#include <queue>

#include "api/player_interface.h"
#include "base/sequence_checker.h"
#include "base/task_util/task_runner.h"
#include "base/task_util/task_runner_factory.h"
#include "base/thread_annotation.h"
#include "media/foundation/media_frame.h"

namespace ave {
namespace player {

/**
 * @brief Base class for AV rendering components (Video, Audio, Subtitle).
 *        Provides timestamp management and frame rendering scheduling.
 */
class AVPRender {
 public:
  /**
   * @brief Constructs AVPRender with task runner and sync controller.
   * @param task_runner_factory Factory for creating task runners.
   * @param avsync_controller Pointer to the AV sync controller.
   */
  AVPRender(base::TaskRunnerFactory* task_runner_factory,
            IAVSyncController* avsync_controller);

  virtual ~AVPRender();

  /**
   * @brief Renders a media frame with proper timing.
   * @param frame The media frame to render.
   */
  void RenderFrame(std::shared_ptr<media::MediaFrame> frame) EXCLUDES(mutex_);

  /**
   * @brief Gets the current timestamp.
   * @return Current timestamp in microseconds.
   */
  int64_t GetCurrentTimeStamp() const EXCLUDES(mutex_);

  /**
   * @brief Starts the renderer.
   */
  virtual void Start() EXCLUDES(mutex_);

  /**
   * @brief Stops the renderer.
   */
  virtual void Stop() EXCLUDES(mutex_);

  /**
   * @brief Pauses the renderer.
   */
  virtual void Pause() EXCLUDES(mutex_);

  /**
   * @brief Resumes the renderer.
   */
  virtual void Resume() EXCLUDES(mutex_);

  /**
   * @brief Flushes all pending frames.
   */
  virtual void Flush() EXCLUDES(mutex_);

 protected:
  /**
   * @brief Internal frame rendering method to be implemented by subclasses.
   * @param frame The media frame to render.
   * @param render Whether to render the frame.
   * @return Next frame delay in microseconds, or 0 if no more frames.
   */
  virtual uint64_t RenderFrameInternal(std::shared_ptr<media::MediaFrame> frame,
                                       bool render) = 0;

  /**
   * @brief Gets the AV sync controller.
   * @return Pointer to the AV sync controller.
   */
  IAVSyncController* GetAVSyncController() const REQUIRES(mutex_) {
    return avsync_controller_;
  }

  /**
   * @brief Checks if the renderer is running.
   * @return True if running, false otherwise.
   */
  bool IsRunning() const EXCLUDES(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
  }

  /**
   * @brief Checks if the renderer is paused.
   * @return True if paused, false otherwise.
   */
  bool IsPaused() const EXCLUDES(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
  }

  mutable std::mutex mutex_;
  std::unique_ptr<base::TaskRunner> task_runner_;
  // Frame queue management
  std::queue<std::shared_ptr<media::MediaFrame>> frame_queue_
      GUARDED_BY(mutex_);

 private:
  /**
   * @brief Schedules the next frame from the queue for rendering.
   */
  void ScheduleNextFrame(uint32_t delay_us = 0) REQUIRES(mutex_);

  /**
   * @brief Handles the scheduled render task.
   * @param update_generation The generation when the task was scheduled.
   */
  void OnRenderTask(int64_t update_generation) EXCLUDES(mutex_);

  /**
   * @brief Calculates render delay for a frame.
   * @param frame The media frame to calculate delay for.
   * @return Late in microseconds.
   */
  int64_t CalculateRenderLateUs(const std::shared_ptr<media::MediaFrame>& frame)
      REQUIRES(mutex_);

  IAVSyncController* avsync_controller_ GUARDED_BY(mutex_);
  int64_t update_generation_ GUARDED_BY(mutex_);
  bool running_ GUARDED_BY(mutex_);
  bool paused_ GUARDED_BY(mutex_);

  static constexpr size_t kMaxQueueSize = 100;  // Prevent memory overflow
};

}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_AVP_RENDER_H_H_ */
