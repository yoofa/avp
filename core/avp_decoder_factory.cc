/*
 * avp_decoder_factory.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_decoder_factory.h"

#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "media/foundation/media_meta.h"

#include "avp_decoder.h"
#include "avp_passthrough_decoder.h"
#include "avp_subtitle_decoder.h"
#include "avp_tunnel_decoder.h"

namespace ave {
namespace player {

namespace {

bool NoNeedDecode(const std::shared_ptr<MediaMeta>& format) {
  if (!format) {
    return false;
  }

  auto mime = format->mime();
  // Audio formats that typically do not need decoding
  static const std::vector<std::string> no_decode_mimes = {"audio/raw",
                                                           "audio/pcm"};

  for (const auto& supported : no_decode_mimes) {
    if (mime.find(supported) != std::string::npos) {
      return true;
    }
  }

  static const std::vector<ave::media::CodecId> no_decode_codecs = {
      ave::media::CodecId::AVE_CODEC_ID_PCM_S16LE,
      ave::media::CodecId::AVE_CODEC_ID_PCM_S16BE,
  };

  for (const auto& codec : no_decode_codecs) {
    if (format->codec() == codec) {
      return true;
    }
  }

  return false;
}

}  // namespace

std::shared_ptr<AVPDecoderBase> AVPDecoderFactory::CreateDecoder(
    std::shared_ptr<media::CodecFactory> codec_factory,
    std::shared_ptr<Message> notify,
    std::shared_ptr<ContentSource> source,
    std::shared_ptr<AVPRender> render,
    const std::shared_ptr<MediaMeta>& format,
    std::shared_ptr<VideoRender> video_render,
    DecoderType decoder_type) {
  if (!format) {
    AVE_LOG(LS_ERROR) << "CreateDecoder: format is null";
    return nullptr;
  }

  auto mime = format->mime();
  AVE_LOG(LS_INFO) << "CreateDecoder: mime=" << mime
                   << ", type=" << decoder_type;

  // Override decoder type based on format if needed
  if (decoder_type == DECODER_NORMAL) {
    decoder_type = DetermineDecoderType(format);
  }

  switch (decoder_type) {
    case DECODER_NORMAL: {
      auto decoder =
          std::make_shared<AVPDecoder>(codec_factory, notify, source, render);
      return decoder;
    }

    case DECODER_PASSTHROUGH: {
      if (!SupportsPassthrough(format)) {
        AVE_LOG(LS_WARNING) << "Format doesn't support passthrough, falling "
                               "back to normal decoder";
        auto decoder =
            std::make_shared<AVPDecoder>(codec_factory, notify, source, render);
        return decoder;
      }
      auto decoder =
          std::make_shared<AVPPassthroughDecoder>(notify, source, render);
      return decoder;
    }

    case DECODER_TUNNEL: {
      if (!SupportsTunnel(format)) {
        AVE_LOG(LS_WARNING) << "Format doesn't support tunnel mode, falling "
                               "back to normal decoder";
        auto decoder =
            std::make_shared<AVPDecoder>(codec_factory, notify, source, render);
        return decoder;
      }
      if (!video_render) {
        AVE_LOG(LS_ERROR) << "Tunnel decoder requires video render";
        return nullptr;
      }
      auto decoder = std::make_shared<AVPTunnelDecoder>(codec_factory, notify,
                                                        source, video_render);
      return decoder;
    }

    case DECODER_SUBTITLE: {
      auto decoder =
          std::make_shared<AVPSubtitleDecoder>(notify, source, render);
      return std::static_pointer_cast<AVPDecoderBase>(decoder);
    }

    default:
      AVE_LOG(LS_ERROR) << "Unknown decoder type: " << decoder_type;
      return nullptr;
  }
}

AVPDecoderFactory::DecoderType AVPDecoderFactory::DetermineDecoderType(
    const std::shared_ptr<MediaMeta>& format,
    bool perfer_passthrough,
    bool is_tunnel) {
  if (!format) {
    return DECODER_NORMAL;
  }

  auto mime = format->mime();

  // Check for subtitle formats
  if (mime.find("text/") != std::string::npos ||
      mime.find("subtitle/") != std::string::npos ||
      mime.find("application/x-subrip") != std::string::npos ||
      mime.find("application/x-ass") != std::string::npos ||
      mime.find("application/x-vtt") != std::string::npos) {
    return DECODER_SUBTITLE;
  }

  // Check for tunnel mode
  if (is_tunnel && SupportsTunnel(format)) {
    return DECODER_TUNNEL;
  }

  // Check for passthrough mode
  if (perfer_passthrough && SupportsPassthrough(format)) {
    return DECODER_PASSTHROUGH;
  }

  // no need decoding format is also use passthrough
  if (NoNeedDecode(format)) {
    return DECODER_PASSTHROUGH;
  }

  return DECODER_NORMAL;
}

bool AVPDecoderFactory::SupportsPassthrough(
    const std::shared_ptr<MediaMeta>& format) {
  return true;
}

bool AVPDecoderFactory::SupportsTunnel(
    const std::shared_ptr<MediaMeta>& format) {
  if (!format) {
    return false;
  }

  auto mime = format->mime();

  // Video formats that typically support tunnel mode
  static const std::vector<std::string> tunnel_formats = {
      "video/avc",           "video/hevc",    "video/h264",
      "video/h265",          "video/mp4v-es", "video/x-vnd.on2.vp8",
      "video/x-vnd.on2.vp9", "video/av01"};

  for (const auto& supported : tunnel_formats) {
    if (mime.find(supported) != std::string::npos) {
      return true;
    }
  }

  return false;
}

}  // namespace player
}  // namespace ave
