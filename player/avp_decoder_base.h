/*
 * avp_decoder_base.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_DECODER_BASE_H
#define AVP_DECODER_BASE_H

#include "media/foundation/av_synchronize_render.h"
#include "media/foundation/handler.h"
#include "media/video/video_render.h"

#include "api/content_source/content_source.h"

using ::ave::media::AVSynchronizeRender;
using ::ave::media::Handler;
using ::ave::media::Looper;
using ::ave::media::Message;
using ::ave::media::VideoRender;

namespace ave {
namespace player {

class AVPDecoderBase : public Handler {
 public:
  enum {
    kWhatDecoderError = 'decE',
  };
  explicit AVPDecoderBase(
      std::shared_ptr<Message> notify,
      std::shared_ptr<ContentSource> source,
      std::shared_ptr<AVSynchronizeRender> synchronizer = nullptr);
  ~AVPDecoderBase() override;

  void Init();
  void Configure(const std::shared_ptr<MediaFormat>& format);
  void SetParameters(const std::shared_ptr<Message>& parameters);
  void SetSynchronizer(const std::shared_ptr<AVSynchronizeRender> synchronizer);
  void SetVideoRender(const std::shared_ptr<VideoRender> video_render);
  void Start();
  // synchronize pause
  void Pause();
  void Resume();
  void Flush();
  void Shutdown();

  void ReportError(status_t err);

 protected:
  enum {
    // internal event for public API
    kWhatConfigure = 'conf',
    kWhatSetParameters = 'setP',
    kWhatSetSynchronizer = 'setS',
    kWhatSetVideoRender = 'setV',
    kWhatStart = 'star',
    kWhatPause = 'paus',
    kWhatResume = 'resu',
    kWhatFlush = 'flus',
    kWhatShutdown = 'shuD',

    // internal input buffer event
    kWhatRequestInputBuffers = 'reqI',
  };

  // internal message handler function
  virtual void OnConfigure(const std::shared_ptr<MediaFormat>& format) = 0;
  virtual void OnSetParameters(const std::shared_ptr<Message>& params) = 0;
  virtual void OnSetSynchronizer(
      const std::shared_ptr<AVSynchronizeRender>& synchronizer) = 0;
  virtual void OnSetVideoRender(
      const std::shared_ptr<VideoRender>& video_render) = 0;
  virtual void OnStart() = 0;
  virtual void OnPause() = 0;
  virtual void OnResume() = 0;
  virtual void OnFlush() = 0;
  virtual void OnShutdown() = 0;

  // input buffer flow
  void OnRequestInputBuffers();
  virtual bool DoRequestInputBuffers() = 0;

  // Handler
  void onMessageReceived(const std::shared_ptr<Message>& msg) override;

  std::shared_ptr<Message> notify_;
  std::shared_ptr<ContentSource> source_;
  std::shared_ptr<AVSynchronizeRender> synchronizer_;

 private:
  std::shared_ptr<Looper> looper_;

  // request_input_buffers_pending_ always accessed from the same thread
  bool request_input_buffers_pending_;
};

}  // namespace player
}  // namespace ave

#endif /* !AVP_DECODER_BASE_H */
