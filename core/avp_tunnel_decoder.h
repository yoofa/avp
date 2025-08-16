/*
 * avp_tunnel_decoder.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_AVP_AVP_TUNNEL_DECODER_H_H_
#define AVE_AVP_AVP_TUNNEL_DECODER_H_H_

#include <list>
#include <memory>

#include "media/codec/codec.h"
#include "media/codec/codec_factory.h"
#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"

#include "avp_decoder_base.h"

using ave::media::Codec;
using ave::media::CodecBuffer;
using ave::media::CodecCallback;
using ave::media::CodecFactory;
using ave::media::MediaFrame;
using ave::media::MediaMeta;

namespace ave {
namespace player {

/**
 * @brief Tunnel decoder for hardware-accelerated video playback
 *
 * This decoder is used when video needs to be rendered directly by hardware
 * (e.g., Android MediaCodec with Surface). The decoded frames are automatically
 * rendered by the hardware, so no additional rendering step is needed.
 */
class AVPTunnelDecoder : public AVPDecoderBase, public CodecCallback {
 public:
  enum {
    kWhatTunnelError = 'tunE',
    kWhatTunnelFormatChanged = 'tunF',
  };

  explicit AVPTunnelDecoder(std::shared_ptr<CodecFactory> codec_factory,
                            std::shared_ptr<Message> notify,
                            std::shared_ptr<ContentSource> source,
                            std::shared_ptr<VideoRender> video_render);
  ~AVPTunnelDecoder() override;

 protected:
  void onMessageReceived(const std::shared_ptr<Message>& msg) override;

 private:
  enum {
    // internal event for codec notify
    kWhatInputBufferAvailable = 'inAv',
    kWhatOutputBufferAvailable = 'outA',
    kWhatDecodingFormatChange = 'fmtC',
    kWhatDecodingError = 'ddEr',
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

  void onRequestInputBuffers();
  void FillCodecBuffer(std::shared_ptr<CodecBuffer>& buffer);
  bool DoRequestInputBuffers() override;

  // CodecCallback
  void OnInputBufferAvailable(size_t index) override;
  void OnOutputBufferAvailable(size_t index) override;
  void OnOutputFormatChanged(const std::shared_ptr<MediaMeta>& format) override;
  void OnError(status_t err) override;
  void OnFrameRendered(std::shared_ptr<Message> notify) override;

  // CodecCallback event handler
  void HandleAnInputBuffer(size_t index);
  void HandleAnOutputBuffer(size_t index);
  void HandleAnOutputFormatChanged(const std::shared_ptr<MediaMeta>& format);
  void HandleAnCodecError(status_t err);

  std::shared_ptr<CodecFactory> codec_factory_;
  std::shared_ptr<VideoRender> video_render_;
  std::shared_ptr<Codec> decoder_;
  std::string codec_name_;

  std::list<std::shared_ptr<MediaFrame>> input_packet_queue_;
};

}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_AVP_TUNNEL_DECODER_H_H_ */
