/*
 * avp_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_DECODER_H
#define AVP_DECODER_H

#include "api/content_source.h"
#include "media/codec/codec.h"
#include "media/codec/codec_factory.h"
#include "media/foundation/av_synchronize_render.h"
#include "media/foundation/handler.h"
#include "media/foundation/video_sink.h"

namespace avp {

using ave::media::AVSynchronizeRender;
using ave::media::Codec;
using ave::media::CodecBuffer;
using ave::media::CodecCallback;
using ave::media::CodecFactory;
using ave::media::Handler;
using ave::media::Looper;
using ave::media::MediaPacket;
using ave::media::Message;
using ave::media::VideoSink;

class AvpDecoder : public Handler, public CodecCallback {
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

  explicit AvpDecoder(std::shared_ptr<Message> notify,
                      std::shared_ptr<ContentSource> source,
                      std::shared_ptr<AVSynchronizeRender> render = nullptr,
                      std::shared_ptr<VideoSink> videoSink = nullptr);
  ~AvpDecoder() override;

  void Init();

  void Configure(const std::shared_ptr<Message>& format);
  void SetParameters(const std::shared_ptr<Message>& parameters);
  void SetRender(const std::shared_ptr<AVSynchronizeRender> render);
  void SetVideoSink(const std::shared_ptr<VideoSink> sink);
  void Start();
  void Pause();
  void Resume();
  void Flush();
  void Shutdown();

 private:
  friend class CodecCallback;
  enum {
    kWhatConfigure = 'conf',
    kWhatSetParameters = 'setP',
    kWhatSetRenderer = 'setR',
    kWhatSetVideoSink = 'setV',
    kWhatStart = 'star',
    kWhatPause = 'paus',
    kWhatResume = 'resu',
    kWhatRequestInputBuffers = 'reqB',
    kWhatFlush = 'flus',
    kWhatShutdown = 'shuD',

    // codec notify
    kWhatInputBufferAvailable = 'inAv',
    kWhatOutputBufferAvailable = 'outA',
    kWhatDecodingFormatChange = 'fmtC',
    kWhatDecodingError = 'ddEr',
  };

  void HandleAnInputBuffer(size_t index);
  void HandleAnOutputBuffer(size_t index);

  status_t FetchInputBuffer(std::shared_ptr<Message>& message);
  bool DoRequestInputBuffers();
  void HandleError(status_t err);

  void OnConfigure(const std::shared_ptr<Message>& format);
  void OnSetParameters(const std::shared_ptr<Message>& params);
  void OnStart();
  void OnPause();
  void OnResume();
  void OnFlush();
  void OnShutdown();

  bool IsSated();
  // return message consumed
  bool OnInputBufferFetched(const std::shared_ptr<MediaPacket>& packet);
  void OnRequestInputBuffers();

  // CodecCallback
  void OnInputBufferAvailable(size_t index) override;
  void OnOutputBufferAvailable(size_t index) override;
  void OnOutputFormatChanged(const std::shared_ptr<Message>& format) override;
  void OnError(status_t err) override;
  void OnFrameRendered(std::shared_ptr<Message> notify) override;

  // Handler
  void onMessageReceived(const std::shared_ptr<Message>& message) override;

  std::shared_ptr<Looper> looper_;
  std::shared_ptr<Message> notify_;
  bool request_input_buffers_pending_;
  std::shared_ptr<ContentSource> source_;
  std::shared_ptr<AVSynchronizeRender> render_;
  std::shared_ptr<VideoSink> video_sink_;
  std::shared_ptr<CodecFactory> codec_factory_;
  std::shared_ptr<Codec> decoder_;
  bool is_audio_;
  std::vector<std::shared_ptr<MediaPacket>> input_packet_queue_;
};
} /* namespace avp */

#endif /* !AVP_DECODER_H */
