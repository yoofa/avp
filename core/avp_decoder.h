/*
 * avp_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_DECODER_H
#define AVP_DECODER_H

#include <list>
#include <vector>

#include "media/codec/codec.h"
#include "media/codec/codec_factory.h"
#include "media/foundation/handler.h"
#include "media/foundation/media_meta.h"

#include "../api/content_source/content_source.h"
#include "avp_decoder_base.h"
#include "avp_render.h"

using ave::media::Codec;
using ave::media::CodecBuffer;
using ave::media::CodecCallback;
using ave::media::CodecFactory;
using ave::media::Handler;
using ave::media::Looper;
using ave::media::MediaFrame;
using ave::media::Message;

namespace ave {
namespace player {

class AVPDecoder : public AVPDecoderBase, public CodecCallback {
 public:
  enum {
    kWhatInputDiscontinuity = 'inDi',
    kWhatVideoSizeChanged = 'viSC',
    kWhatFlushCompleted = 'flsC',
    kWhatShutdownCompleted = 'shDC',
    kWhatResumeCompleted = 'resC',
    kWhatEOS = 'eos ',
    kWhatError = 'err ',

    kWhatRenderBuffer = 'rndr',
    kWhatSetVideoSurface = 'sSur',
    kWhatAudioOutputFormatChanged = 'aofc',
    kWhatDrmReleaseCrypto = 'rDrm',
  };

  explicit AVPDecoder(std::shared_ptr<CodecFactory> codec_factory,
                      std::shared_ptr<Message> notify,
                      std::shared_ptr<ContentSource> source,
                      std::shared_ptr<AVPRender> render);
  ~AVPDecoder() override;

 protected:
  void onMessageReceived(const std::shared_ptr<Message>& msg) override;

 private:
  enum class CodecState {
    kUnconfigured,
    kConfigured,
    kRunning,
    kPaused,
    kStopping,
    kStopped,
  };

  enum {
    // internal event for codec notify
    kWhatInputBufferAvailable = 'inAv',
    kWhatOutputBufferAvailable = 'outA',
    kWhatDecodingFormatChange = 'fmtC',
    kWhatDecodingError = 'ddEr',
    kWhatFrameRendered = 'frRd',
    // retry when no input data was available
    kWhatRetryInputBuffer = 'retI',
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

  status_t FillCodecBuffer(const std::shared_ptr<CodecBuffer>& buffer);

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
  std::shared_ptr<Codec> decoder_;
  std::shared_ptr<VideoRender> video_render_;
  std::string codec_name_;

  bool is_audio_;
  CodecState codec_state_ = CodecState::kUnconfigured;
  std::list<std::shared_ptr<MediaFrame>> input_packet_queue_;
  // The codec owns these buffers while paused. Retain their indices and
  // consume them only after resume; dropping the callback would permanently
  // starve the codec of that buffer.
  std::vector<size_t> pending_input_buffer_indices_;
  std::vector<size_t> pending_output_buffer_indices_;
  bool eos_reached_ = false;
  bool eos_sent_ = false;
};

}  // namespace player
}  // namespace ave

#endif /* !AVP_DECODER_H */
