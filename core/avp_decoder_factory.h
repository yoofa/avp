/*
 * avp_decoder_factory.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_AVP_AVP_DECODER_FACTORY_H_H_
#define AVE_AVP_AVP_DECODER_FACTORY_H_H_

#include <memory>

#include "media/codec/codec_factory.h"
#include "media/foundation/media_meta.h"
#include "media/video/video_render.h"

#include "api/content_source/content_source.h"

#include "avp_decoder_base.h"
#include "avp_render.h"

namespace ave {
namespace player {

/**
 * @brief Factory for creating different types of decoders
 *
 * This factory creates the appropriate decoder based on the media format
 * and playback requirements (normal, passthrough, tunnel, subtitle).
 */
class AVPDecoderFactory {
 public:
  enum DecoderType {
    DECODER_NORMAL,       // Regular software/hardware decoder
    DECODER_PASSTHROUGH,  // Audio passthrough decoder
    DECODER_TUNNEL,       // Hardware tunnel decoder
    DECODER_SUBTITLE,     // Subtitle decoder
  };

  /**
   * @brief Creates a decoder based on the media format and requirements
   * @param codec_factory The codec factory for creating codecs
   * @param notify The notification message handler
   * @param source The content source
   * @param render The AV renderer
   * @param format The media format
   * @param video_render The video renderer (for tunnel mode)
   * @param decoder_type The type of decoder to create
   * @return A shared pointer to the created decoder
   */
  static std::shared_ptr<AVPDecoderBase> CreateDecoder(
      std::shared_ptr<media::CodecFactory> codec_factory,
      std::shared_ptr<Message> notify,
      std::shared_ptr<ContentSource> source,
      std::shared_ptr<AVPRender> render,
      const std::shared_ptr<MediaMeta>& format,
      std::shared_ptr<VideoRender> video_render = nullptr,
      DecoderType decoder_type = DECODER_NORMAL);

  /**
   * @brief Determines the appropriate decoder type based on format and
   * requirements
   * @param format The media format
   * @param is_passthrough Whether passthrough mode is requested
   * @param is_tunnel Whether tunnel mode is requested
   * @return The appropriate decoder type
   */
  static DecoderType DetermineDecoderType(
      const std::shared_ptr<MediaMeta>& format,
      bool is_passthrough = false,
      bool is_tunnel = false);

  /**
   * @brief Checks if a format supports passthrough mode
   * @param format The media format to check
   * @return True if passthrough is supported
   */
  static bool SupportsPassthrough(const std::shared_ptr<MediaMeta>& format);

  /**
   * @brief Checks if a format supports tunnel mode
   * @param format The media format to check
   * @return True if tunnel mode is supported
   */
  static bool SupportsTunnel(const std::shared_ptr<MediaMeta>& format);

 private:
  AVPDecoderFactory() = delete;
  ~AVPDecoderFactory() = delete;
};

}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_AVP_DECODER_FACTORY_H_H_ */
