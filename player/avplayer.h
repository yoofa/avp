/*
 * avplayer.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_AVPLAYER_H
#define AVP_AVPLAYER_H

#include <memory>

#include "common/handler.h"
#include "common/looper.h"
#include "common/message.h"

#include "base/system/avp_export.h"
#include "player/audio_decoder.h"
#include "player/audio_decoder_factory.h"
#include "player/default_Audio_decoder_factory.h"
#include "player/default_video_decoder_factory.h"
#include "player/player_interface.h"
#include "player/video_decoder.h"
#include "player/video_decoder_factory.h"

namespace avp {
AVP_EXPORT class AvPlayer : public PlayerBase, public Handler {
 public:
  AvPlayer();
  AvPlayer(std::shared_ptr<AudioDecoderFactory> audioDecoderFactory,
           std::shared_ptr<VideoDecoderFactory> videoDecoderFactory);
  virtual ~AvPlayer() override;

  virtual status_t setListener(
      const std::shared_ptr<Listener>& listener) override;
  virtual status_t init() override;
  status_t setDataSource(const char* url) override;
  virtual status_t setDataSource(int fd,
                                 int64_t offset,
                                 int64_t length) override;
  virtual status_t setDataSource(
      const std::shared_ptr<ContentSource>& source) override;

  virtual status_t setAudioRender(
      std::shared_ptr<AudioSink> audioRender) override;
  virtual status_t setVideoRender(
      std::shared_ptr<VideoSink> videorender) override;

  virtual status_t prepare() override;
  virtual status_t start() override;
  virtual status_t stop() override;
  virtual status_t pause() override;
  virtual status_t resume() override;
  virtual status_t seekTo(
      int msec,
      SeekMode mode = SeekMode::SEEK_PREVIOUS_SYNC) override;
  virtual status_t reset() override;

  void onSourceNotify(const std::shared_ptr<Message>& msg);

 protected:
  void onMessageReceived(const std::shared_ptr<Message>& message) override;

 private:
  void performReset();

  enum {
    kWhatSetDataSource = '=DaS',
    kWhatPrepare = 'prep',
    kWhatSetVideoSurface = '=VSu',
    kWhatSetAudioSink = '=AuS',
    kWhatMoreDataQueued = 'more',
    kWhatConfigPlayback = 'cfPB',
    kWhatConfigSync = 'cfSy',
    kWhatGetPlaybackSettings = 'gPbS',
    kWhatGetSyncSettings = 'gSyS',
    kWhatStart = 'strt',
    kWhatScanSources = 'scan',
    kWhatVideoNotify = 'vidN',
    kWhatAudioNotify = 'audN',
    kWhatClosedCaptionNotify = 'capN',
    kWhatRendererNotify = 'renN',
    kWhatReset = 'rset',
    kWhatNotifyTime = 'nfyT',
    kWhatSeek = 'seek',
    kWhatPause = 'paus',
    kWhatResume = 'rsme',
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

  std::shared_ptr<AudioDecoderFactory> mAudioDecoderFactory;
  std::shared_ptr<VideoDecoderFactory> mVideoDecoderFactory;
  std::shared_ptr<Looper> mPlayerLooper;

  std::shared_ptr<ContentSource> mSource;
};

}  // namespace avp

#endif /* !AVP_AVPLAYER_H */
