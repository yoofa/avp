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
#include "base/task_util/task_runner.h"
#include "base/task_util/task_runner_factory.h"
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
  void RenderFrame(std::shared_ptr<media::MediaFrame> frame);

  /**
   * @brief Gets the current timestamp.
   * @return Current timestamp in microseconds.
   */
  int64_t GetCurrentTimeStamp() const;

  /**
   * @brief Starts the renderer.
   */
  virtual void Start();

  /**
   * @brief Stops the renderer.
   */
  virtual void Stop();

  /**
   * @brief Pauses the renderer.
   */
  virtual void Pause();

  /**
   * @brief Resumes the renderer.
   */
  virtual void Resume();

  /**
   * @brief Flushes all pending frames.
   */
  virtual void Flush();

 protected:
  /**
   * @brief Internal frame rendering method to be implemented by subclasses.
   * @param frame The media frame to render.
   * @param render Whether to render the frame.
   */
  virtual void RenderFrameInternal(std::shared_ptr<media::MediaFrame> frame,
                                   bool render) = 0;

  /**
   * @brief Gets the task runner.
   * @return Pointer to the task runner.
   */
  base::TaskRunner* GetTaskRunner() const { return task_runner_.get(); }

  /**
   * @brief Gets the AV sync controller.
   * @return Pointer to the AV sync controller.
   */
  IAVSyncController* GetAVSyncController() const { return avsync_controller_; }

  /**
   * @brief Checks if the renderer is running.
   * @return True if running, false otherwise.
   */
  bool IsRunning() const { return running_; }

  /**
   * @brief Checks if the renderer is paused.
   * @return True if paused, false otherwise.
   */
  bool IsPaused() const { return paused_; }

 private:
  /**
   * @brief Schedules the next frame from the queue for rendering.
   */
  void ScheduleNextFrame() REQUIRES(mutex_);

  /**
   * @brief Handles the scheduled render task.
   * @param update_generation The generation when the task was scheduled.
   */
  void OnRenderTask(int64_t update_generation);

  /**
   * @brief Calculates render delay for a frame.
   * @param frame The media frame to calculate delay for.
   * @return Delay in microseconds.
   */
  int64_t CalculateRenderDelay(const std::shared_ptr<media::MediaFrame>& frame);

  mutable std::mutex mutex_;
  std::unique_ptr<base::TaskRunner> task_runner_;
  IAVSyncController* avsync_controller_;
  int64_t update_generation_ GUARDED_BY(mutex_);
  bool running_;
  bool paused_;

  // Frame queue management
  std::queue<std::shared_ptr<media::MediaFrame>> frame_queue_
      GUARDED_BY(mutex_);
  static constexpr size_t kMaxQueueSize = 100;  // Prevent memory overflow
};

}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_AVP_RENDER_H_H_ */
