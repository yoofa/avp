/*
 * avplayer.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_AVPLAYER_H
#define AVP_AVPLAYER_H

#include <memory>
#include <unordered_map>
#include <vector>

#include "base/system/ave_export.h"

#include "media/audio/audio_device.h"
#include "media/codec/codec_factory.h"
#include "media/foundation/handler.h"
#include "media/foundation/looper.h"
#include "media/foundation/message.h"

#include "api/content_source/content_source.h"
#include "api/player.h"

#include "avp_audio_render.h"
#include "avp_decoder.h"
#include "avp_decoder_factory.h"
#include "avp_video_render.h"
#include "avsync_controller.h"

namespace ave {
namespace player {

using ave::media::AudioDevice;
using ave::media::CodecFactory;
using ave::media::Handler;
using ave::media::Looper;
using ave::media::Message;

// Forward declarations for deferred actions
class Action;
class SeekAction;
class ResumeDecoderAction;
class FlushDecoderAction;
class SimpleAction;

AVE_EXPORT class AvPlayer : public Player,
                            public Handler,
                            public ContentSource::Notify {
 public:
  explicit AvPlayer(
      std::shared_ptr<ContentSourceFactory> content_source_factory,
      std::shared_ptr<DemuxerFactory> demuxer_factory,
      std::shared_ptr<CodecFactory> codec_factory,
      std::shared_ptr<AudioDevice> audio_device);

  ~AvPlayer() override;

  status_t SetListener(std::shared_ptr<Listener> listener) override;
  status_t Init() override;
  status_t SetDataSource(
      const char* url,
      const std::unordered_map<std::string, std::string>& headers) override;
  status_t SetDataSource(int fd, int64_t offset, int64_t length) override;
  status_t SetDataSource(std::shared_ptr<ave::DataSource> data_source) override;
  status_t SetDataSource(std::shared_ptr<ContentSource> source) override;
  status_t SetVideoRender(std::shared_ptr<VideoRender> video_render) override;

  // High-level control similar to ExoPlayer/NuPlayer
  status_t Prepare() override;
  status_t Start()
      override;  // Starts source, instantiates decoders, starts renderers
  status_t Stop() override;    // Stops and releases decoders and renderers
  status_t Pause() override;   // Pauses source, decoders, renderers
  status_t Resume() override;  // Resumes decoders and renderers
  status_t SeekTo(int msec, SeekMode mode) override;
  status_t Reset() override;

 private:
  enum {
    kWhatSetDataSource = '=DaS',
    kWhatSetVideoRender = '=Vdr',
    kWhatPrepare = 'prep',
    kWhatStart = 'strt',
    kWhatStop = 'stop',
    kWhatSeek = 'seek',
    kWhatPause = 'paus',
    kWhatResume = 'rsme',
    kWhatReset = 'rset',

    // Source Notify Message
    kWhatSourcePrepared = 'sPre',
    kWhatSourceFlagsChanged = 'sFlg',
    kWhatSourceVideoSizeChanged = 'sVsz',
    kWhatSourceSeekComplete = 'sSkC',
    kWhatSourceBufferingStart = 'sBfS',
    kWhatSourceBufferingUpdate = 'sBfU',
    kWhatSourceBufferingEnd = 'sBfE',
    kWhatSourceCompletion = 'sCmp',
    kWhatSourceError = 'sErr',
    kWhatSourceFetchData = 'sFch',

    kWhatMoreDataQueued = 'more',
    kWhatConfigPlayback = 'cfPB',
    kWhatConfigSync = 'cfSy',
    kWhatGetPlaybackSettings = 'gPbS',
    kWhatGetSyncSettings = 'gSyS',
    kWhatScanSources = 'scan',
    kWhatVideoNotify = 'vidN',
    kWhatAudioNotify = 'audN',
    kWhatClosedCaptionNotify = 'capN',
    kWhatRendererNotify = 'renN',
    kWhatNotifyTime = 'nfyT',
    kWhatPollDuration = 'polD',
    kWhatSourceNotify = 'srcN',
    kWhatGetTrackInfo = 'gTrI',
    kWhatGetSelectedTrack = 'gSel',
    kWhatSelectTrack = 'selT',
    kWhatGetBufferingSettings = 'gBus',
    kWhatSetBufferingSettings = 'sBuS',
    kWhatPrepareDrm = 'pDrm',
    kWhatReleaseDrm = 'rDrm',
    kWhatMediaClockNotify = 'mckN',
  };

  // Flush status tracking
  enum FlushStatus {
    NONE,
    FLUSHING_DECODER,
    FLUSHING_DECODER_SHUTDOWN,
    SHUTTING_DOWN_DECODER,
    FLUSHED,
    SHUT_DOWN
  };

  enum FlushCommand { FLUSH_CMD_NONE, FLUSH_CMD_FLUSH, FLUSH_CMD_SHUTDOWN };

  // Action base class for deferred actions
  class Action {
   public:
    Action() = default;
    virtual ~Action() = default;
    virtual void Execute(AvPlayer* player) = 0;
  };

  // Seek action
  class SeekAction : public Action {
   public:
    explicit SeekAction(int64_t seek_time_us, SeekMode seek_mode)
        : seek_time_us_(seek_time_us), seek_mode_(seek_mode) {}

    void Execute(AvPlayer* player) override;

   private:
    int64_t seek_time_us_;
    SeekMode seek_mode_;
  };

  // Resume decoder action
  class ResumeDecoderAction : public Action {
   public:
    explicit ResumeDecoderAction(bool need_notify)
        : need_notify_(need_notify) {}

    void Execute(AvPlayer* player) override;

   private:
    bool need_notify_;
  };

  // Flush decoder action
  class FlushDecoderAction : public Action {
   public:
    FlushDecoderAction(FlushCommand audio, FlushCommand video)
        : audio_(audio), video_(video) {}

    void Execute(AvPlayer* player) override;

   private:
    FlushCommand audio_;
    FlushCommand video_;
  };

  // Set video render sink action
  class SetVideoRenderSinkAction : public Action {
   public:
    SetVideoRenderSinkAction(std::shared_ptr<VideoRender> video_render)
        : video_render_(video_render) {}
    void Execute(AvPlayer* player) override;

   private:
    std::shared_ptr<VideoRender> video_render_;
  };

  // Simple action
  class SimpleAction : public Action {
   public:
    using ActionFunc = void (AvPlayer::*)();
    explicit SimpleAction(ActionFunc func) : func_(func) {}

    void Execute(AvPlayer* player) override;

   private:
    ActionFunc func_;
  };

  /******* Source Notify *******/
  void OnPrepared(status_t err) override;
  void OnFlagsChanged(int32_t flags) override;
  void OnVideoSizeChanged(std::shared_ptr<MediaMeta>& format) override;
  void OnSeekComplete() override;
  void OnBufferingStart() override;
  void OnBufferingUpdate(int percent) override;
  void OnBufferingEnd() override;
  void OnCompletion() override;
  void OnError(status_t error) override;
  void OnFetchData(MediaType stream_type) override;

  /****** action methods ******/
  // Internal state machine style handlers
  void OnStart(int64_t start_us = -1,
               SeekMode seek_mode = SeekMode::SEEK_PREVIOUS_SYNC);
  void OnStop();
  void OnPause();
  void OnResume();
  void OnSeek(int64_t seek_to_us, SeekMode seek_mode);

  void OnSourceNotify(const std::shared_ptr<Message>& msg);
  void OnDecoderNotify(const std::shared_ptr<Message>& msg);
  void OnRenderNotify(const std::shared_ptr<Message>& msg);

  void onMessageReceived(const std::shared_ptr<Message>& message) override;

  /****** internal methods ******/
  void PerformSetVideoRender(std::shared_ptr<VideoRender> video_render);
  void PostScanSources();
  status_t InstantiateDecoder(bool audio,
                              std::shared_ptr<AVPDecoderBase>& decoder);
  // New methods for improved state management
  void ProcessDeferredActions();
  void PerformSeek(int64_t seek_time_us, SeekMode seek_mode);
  void PerformDecoderFlush(FlushCommand audio, FlushCommand video);
  void PerformReset();
  void PerformScanSources();
  void PerformResumeDecoders(bool need_notify);
  void FinishResume();
  void NotifyDriverSeekComplete();
  void HandleFlushComplete(bool audio, bool is_decoder);
  void FinishFlushIfPossible();
  void ClearFlushComplete();
  void FlushDecoder(bool audio, bool need_shutdown);
  void CancelPollDuration();
  void SchedulePollDuration();

  //
  std::mutex mutex_;
  std::unique_ptr<base::TaskRunnerFactory> task_runner_factory_;
  std::shared_ptr<ContentSourceFactory> content_source_factory_;
  std::shared_ptr<DemuxerFactory> demuxer_factory_;
  std::shared_ptr<CodecFactory> codec_factory_;
  std::shared_ptr<AudioDevice> audio_device_;
  std::shared_ptr<Looper> player_looper_;
  std::unique_ptr<base::TaskRunner> callback_runner_;

  std::shared_ptr<ContentSource> source_;
  std::shared_ptr<VideoRender> video_render_sink_;

  std::shared_ptr<AVPDecoderBase> audio_decoder_;
  std::shared_ptr<AVPDecoderBase> video_decoder_;
  std::shared_ptr<AVPAudioRender> audio_render_;
  std::shared_ptr<AVPVideoRender> video_render_;
  std::shared_ptr<IAVSyncController> sync_controller_;

  bool started_;
  bool pending_start_with_prepare_async_;
  bool prepared_;
  bool paused_;
  bool paused_for_buffering_;
  bool paused_by_client_;
  bool source_started_;
  bool scan_sources_pending_;
  bool audio_eos_;
  bool video_eos_;
  bool resetting_;
  bool resume_pending_;
  int64_t previous_seek_time_us_;
  int32_t scan_sources_generation_;
  int32_t poll_duration_generation_;

  // Deferred actions for state management
  std::vector<std::shared_ptr<Action>> deferred_actions_;

  FlushStatus flushing_audio_;
  FlushStatus flushing_video_;
  bool flush_complete_[2][2];  // [audio/video][decoder/renderer]

  std::weak_ptr<Listener> listener_;
};

}  // namespace player
}  // namespace ave

#endif /* !AVP_AVPLAYER_H */
