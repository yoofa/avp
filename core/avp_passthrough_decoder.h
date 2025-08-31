/*
 * avp_passthrough_decoder.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_PLAYER_AVP_PASSTHROUGH_DECODER_H_
#define AVE_PLAYER_AVP_PASSTHROUGH_DECODER_H_

#include <condition_variable>
#include <mutex>
#include <queue>

#include "media/foundation/media_frame.h"

#include "avp_decoder_base.h"

using ave::media::MediaFrame;

namespace ave {
namespace player {

/**
 * @brief Passthrough decoder for audio formats that can be played directly
 *
 * This decoder is used for audio formats like AAC, AC3, DTS that can be
 * sent directly to the audio hardware without software decoding.
 * It provides a virtual decoding interface to maintain consistency with
 * the regular decoder flow.
 */
class AVPPassthroughDecoder : public AVPDecoderBase {
 public:
  AVPPassthroughDecoder(const std::shared_ptr<Message>& notify,
                        const std::shared_ptr<ContentSource>& source,
                        const std::shared_ptr<AVPRender>& render);
  ~AVPPassthroughDecoder() override;

 protected:
  // AVPDecoderBase implementation
  void OnConfigure(const std::shared_ptr<MediaMeta>& format) override;
  void OnSetParameters(const std::shared_ptr<Message>& params) override;
  void OnSetVideoRender(
      const std::shared_ptr<VideoRender>& video_render) override;
  void OnStart() override;
  void OnPause() override;
  void OnResume() override;
  void OnFlush() override;
  void OnShutdown() override;

  bool DoRequestInputBuffers() override;
  void onMessageReceived(const std::shared_ptr<Message>& msg) override;

 private:
  enum {
    kWhatBufferConsumed = 'bufc',
  };

  bool IsStaleReply(const std::shared_ptr<Message>& msg);
  bool IsDoneFetching() const;

  std::shared_ptr<MediaFrame> AggregateBuffer(
      const std::shared_ptr<MediaFrame>& packet);
  status_t DequeueAccessUnit(std::shared_ptr<MediaFrame>& packet);
  status_t FetchInputData(std::shared_ptr<MediaFrame>& packet);
  void OnInputBufferFilled(const std::shared_ptr<MediaFrame>& packet);
  void OnBufferConsumed(int32_t size);
  void DoFlush(bool notifyComplete);

  int64_t skip_rendering_until_media_time_us_;
  bool reached_eos_;
  size_t pending_buffers_to_drain_;
  size_t total_bytes_;
  size_t cached_bytes_;

  // Buffer aggregation for better power efficiency
  std::shared_ptr<MediaFrame> aggregate_buffer_;
  std::shared_ptr<MediaFrame> pending_audio_access_unit_;
  status_t pending_audio_err_;

  // For buffer generation tracking
  int32_t buffer_generation_;
};

}  // namespace player
}  // namespace ave

#endif  // AVE_PLAYER_AVP_PASSTHROUGH_DECODER_H_
