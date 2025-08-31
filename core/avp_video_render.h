/*
 * avp_video_render.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_AVP_AVP_VIDEO_RENDER_H_H_
#define AVE_AVP_AVP_VIDEO_RENDER_H_H_

#include <memory>
#include <mutex>

#include "avp_render.h"
#include "media/foundation/media_meta.h"
#include "media/video/video_render.h"

namespace ave {
namespace player {

/**
 * @brief Video renderer implementation that manages video frame rendering
 * through a VideoRender sink. Provides frame dropping, timing control, and
 * format change detection.
 */
class AVPVideoRender : public AVPRender {
 public:
  /**
   * @brief Constructs AVPVideoRender with required parameters.
   * @param task_runner_factory Factory for creating task runners.
   * @param avsync_controller Pointer to the AV sync controller.
   */
  AVPVideoRender(base::TaskRunnerFactory* task_runner_factory,
                 IAVSyncController* avsync_controller);

  ~AVPVideoRender() override;

  /**
   * @brief Sets the video render sink for actual frame rendering.
   * @param video_render Shared pointer to the video render sink.
   */
  void SetSink(std::shared_ptr<media::VideoRender> video_render);

  /**
   * @brief Gets the current video render sink.
   * @return Shared pointer to the video render sink, or nullptr if not set.
   */
  std::shared_ptr<media::VideoRender> GetSink() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return video_render_;
  }

  /**
   * @brief Starts the video renderer.
   */
  void Start() override EXCLUDES(mutex_);

  /**
   * @brief Stops the video renderer.
   */
  void Stop() override EXCLUDES(mutex_);

  /**
   * @brief Pauses the video renderer.
   */
  void Pause() override EXCLUDES(mutex_);

  /**
   * @brief Resumes the video renderer.
   */
  void Resume() override EXCLUDES(mutex_);

  /**
   * @brief Flushes all pending video frames.
   */
  void Flush() override EXCLUDES(mutex_);

 protected:
  /**
   * @brief Internal frame rendering method that handles video data.
   * @param frame The video frame to render.
   * @param consumed True if the frame is full consumed and can be drop from
   * queue.
   * @return Next frame delay in microseconds.
   */
  uint64_t RenderFrameInternal(std::shared_ptr<media::MediaFrame>& frame,
                               bool& consumed) override REQUIRES(mutex_);

 private:
  std::shared_ptr<media::VideoRender> video_render_ GUARDED_BY(mutex_);

  // Video format tracking
  media::MediaMeta current_video_format_ GUARDED_BY(mutex_);
  bool format_initialized_ GUARDED_BY(mutex_);

  // Statistics
  int64_t total_frames_rendered_ GUARDED_BY(mutex_);
  int64_t total_frames_dropped_ GUARDED_BY(mutex_);
  int64_t last_video_pts_us_ GUARDED_BY(mutex_);
};

}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_AVP_VIDEO_RENDER_H_H_ */
