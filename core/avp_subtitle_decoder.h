/*
 * avp_subtitle_decoder.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_AVP_AVP_SUBTITLE_DECODER_H_H_
#define AVE_AVP_AVP_SUBTITLE_DECODER_H_H_

#include <map>
#include <memory>

#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"

#include "avp_decoder_base.h"

using ave::media::MediaFrame;
using ave::media::MediaMeta;

namespace ave {
namespace player {

/**
 * @brief Subtitle decoder for various subtitle formats
 *
 * This decoder handles subtitle formats like SRT, ASS, VTT, etc.
 * It parses subtitle packets and creates subtitle frames for rendering.
 */
class AVPSubtitleDecoder : public AVPDecoderBase {
 public:
  enum {
    kWhatSubtitleError = 'subE',
    kWhatSubtitleFormatChanged = 'subF',
  };

  explicit AVPSubtitleDecoder(std::shared_ptr<Message> notify,
                              std::shared_ptr<ContentSource> source,
                              std::shared_ptr<AVPRender> render);
  ~AVPSubtitleDecoder() override;

 protected:
  void onMessageReceived(const std::shared_ptr<Message>& msg) override;

 private:
  enum {
    kWhatParseSubtitle = 'parS',
  };

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
  void ParseSubtitlePacket(const std::shared_ptr<MediaFrame>& packet);
  void RenderSubtitleFrame(const std::shared_ptr<media::MediaFrame>& frame);

  // Subtitle format specific parsing
  void ParseSRTSubtitle(const std::shared_ptr<MediaFrame>& packet);
  void ParseASSSubtitle(const std::shared_ptr<MediaFrame>& packet);
  void ParseVTTSubtitle(const std::shared_ptr<MediaFrame>& packet);

  std::string subtitle_format_;
  std::map<int64_t, std::shared_ptr<media::MediaFrame>> subtitle_cache_;

  // For subtitle timing
  int64_t current_subtitle_start_;
  int64_t current_subtitle_end_;
  std::shared_ptr<media::MediaFrame> current_subtitle_frame_;
};

}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_AVP_SUBTITLE_DECODER_H_H_ */
