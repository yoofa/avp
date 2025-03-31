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

#include "base/system/ave_export.h"

#include "media/audio/audio_device.h"
#include "media/codec/codec_factory.h"
#include "media/foundation/handler.h"
#include "media/foundation/looper.h"
#include "media/foundation/media_clock.h"
#include "media/foundation/message.h"

#include "api/content_source/content_source.h"
#include "api/player.h"
#include "player/avp_audio_render.h"
#include "player/avp_decoder.h"
#include "player/avp_video_render.h"
#include "player/avsync_controller.h"

namespace ave {
namespace player {

using ave::media::AudioDevice;
using ave::media::CodecFactory;
using ave::media::Handler;
using ave::media::Looper;
using ave::media::MediaClock;
using ave::media::Message;

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

  status_t Prepare() override;
  status_t Start() override;
  status_t Stop() override;
  status_t Pause() override;
  status_t Resume() override;
  status_t SeekTo(int msec, SeekMode mode) override;
  status_t Reset() override;

 private:
  /******* Source Notify *******/
  void OnPrepared(status_t err) override;
  void OnFlagsChanged(int32_t flags) override;
  void OnVideoSizeChanged(std::shared_ptr<MediaFormat>& format) override;
  void OnSeekComplete() override;
  void OnBufferingStart() override;
  void OnBufferingUpdate(int percent) override;
  void OnBufferingEnd() override;
  void OnCompletion() override;
  void OnError(status_t error) override;
  void OnFetchData(MediaType stream_type) override;

  void PostScanSources();
  status_t InstantiateDecoder(bool audio, std::shared_ptr<AVPDecoder>& decoder);
  void PerformReset();

  void OnStart(int64_t startUs = -1,
               SeekMode seekMode = SeekMode::SEEK_PREVIOUS_SYNC);
  void OnStop();
  void OnPause();
  void OnResume();
  void OnSeek(int64_t seek_to_us, SeekMode seek_mode);

  void OnSourceNotify(const std::shared_ptr<Message>& msg);
  void OnDecoderNotify(const std::shared_ptr<Message>& msg);
  void OnRenderNotify(const std::shared_ptr<Message>& msg);

  void onMessageReceived(const std::shared_ptr<Message>& message) override;

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

  std::shared_ptr<ContentSourceFactory> content_source_factory_;
  std::shared_ptr<DemuxerFactory> demuxer_factory_;
  std::shared_ptr<CodecFactory> codec_factory_;
  std::shared_ptr<AudioDevice> audio_device_;

  std::shared_ptr<AVPDecoder> audio_decoder_;
  std::shared_ptr<AVPDecoder> video_decoder_;
  std::shared_ptr<AVPAudioRender> audio_render_;
  std::shared_ptr<AVPVideoRender> video_render_;
  std::shared_ptr<IAVSyncController> sync_controller_;
  std::shared_ptr<Looper> player_looper_;

  std::shared_ptr<MediaClock> media_clock_;
  std::shared_ptr<ContentSource> source_;
  std::shared_ptr<VideoRender> video_render_sink_;

  bool started_;
  bool prepared_;
  bool paused_;
  bool source_started_;
  bool scan_sources_pending_;
  bool audio_eos_;
  bool video_eos_;

  std::weak_ptr<Listener> listener_;
};

}  // namespace player
}  // namespace ave

#endif /* !AVP_AVPLAYER_H */