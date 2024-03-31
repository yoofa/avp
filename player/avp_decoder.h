/*
 * avp_decoder.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_DECODER_H
#define AVP_DECODER_H

#include "media/handler.h"
#include "player/avp_render_synchronizer.h"
#include "player/decoder.h"
#include "player/decoder_factory.h"
#include "player/media_source.h"

namespace avp {

using ave::Handler;
using ave::Looper;
using ave::Message;

class AvpDecoder : public Handler, public Decoder::DecoderCallback {
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
                      std::shared_ptr<PlayerBase::ContentSource> source,
                      std::shared_ptr<AvpRenderSynchronizer> render = nullptr,
                      std::shared_ptr<VideoSink> videoSink = nullptr);
  virtual ~AvpDecoder();

  virtual void init();

  void configure(const std::shared_ptr<Message>& format);
  void setParameters(const std::shared_ptr<Message>& parameters);
  void setRender(const std::shared_ptr<AvpRenderSynchronizer> render);
  void setVideoSink(const std::shared_ptr<VideoSink> sink);
  void start();
  void pause();
  void resume();
  void flush();
  void shutdown();

 private:
  friend DecoderCallback;
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

  void handleAnInputBuffer();
  void handleAnOutputBuffer();

  status_t fetchInputBuffer(std::shared_ptr<Message>& message);
  bool doRequestInputBuffers();
  void handleError(status_t err);

  void onConfigure(const std::shared_ptr<Message>& format);
  void onSetParameters(const std::shared_ptr<Message>& params);
  void onStart();
  void onPause();
  void onResume();
  void onFlush();
  void onShutdown();
  void onMessageReceived(const std::shared_ptr<Message>& message) override;

  bool isSated();
  // return message consumed
  bool onInputBufferFetched(const std::shared_ptr<Message>& message);
  void onRequestInputBuffers();

  // DecoderCallback
  void onInputBufferAvailable() override;
  void onOutputBufferAvailable() override;
  void onFormatChange(std::shared_ptr<Message> format) override;
  void onError(status_t err) override;

  std::shared_ptr<Looper> mLooper;
  std::shared_ptr<Message> mNotify;
  bool mRequestInputBuffersPending;
  std::shared_ptr<PlayerBase::ContentSource> mSource;
  std::shared_ptr<AvpRenderSynchronizer> mRender;
  std::shared_ptr<VideoSink> mVideoSink;
  std::unique_ptr<DecoderFactory> mDecoderFactory;
  std::shared_ptr<Decoder> mDecoder;
  bool mIsAudio;
  std::vector<std::shared_ptr<Message>> mInputBufferMessageQueue;
};
} /* namespace avp */

#endif /* !AVP_DECODER_H */
