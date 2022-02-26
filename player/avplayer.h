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
#include "player/audio_sink.h"
#include "player/avp_decoder.h"
#include "player/avp_render_synchronizer.h"
#include "player/default_Audio_decoder_factory.h"
#include "player/default_video_decoder_factory.h"
#include "player/player_interface.h"
#include "player/video_decoder.h"
#include "player/video_decoder_factory.h"
#include "player/video_sink.h"

namespace avp {
AVP_EXPORT class AvPlayer : public PlayerBase, public Handler {
 public:
  AvPlayer();
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

  virtual status_t setAudioSink(std::shared_ptr<AudioSink> sink) override;
  virtual status_t setVideoSink(std::shared_ptr<VideoSink> sink) override;

  virtual status_t prepare() override;
  virtual status_t start() override;
  virtual status_t stop() override;
  virtual status_t pause() override;
  virtual status_t resume() override;
  virtual status_t seekTo(
      int msec,
      SeekMode mode = SeekMode::SEEK_PREVIOUS_SYNC) override;
  virtual status_t reset() override;

 private:
  void postScanSources();
  status_t instantiateDecoder(bool audio, std::shared_ptr<AvpDecoder>& decoder);
  void performReset();

  void onStart(int64_t startUs = -1,
               SeekMode seekMode = SeekMode::SEEK_PREVIOUS_SYNC);
  void onStop();
  void onPause();
  void onResume();
  void onSeek();

  void onSourceNotify(const std::shared_ptr<Message>& msg);
  void onDecoderNotify(const std::shared_ptr<Message>& msg);
  void onRenderNotify(const std::shared_ptr<Message>& msg);

  void onMessageReceived(const std::shared_ptr<Message>& message) override;

  enum {
    kWhatSetDataSource = '=DaS',
    kWhatPrepare = 'prep',
    kWhatSetVideoSink = '=sVS',
    kWhatSetAudioSink = '=sAS',
    kWhatStart = 'strt',
    kWhatSeek = 'seek',
    kWhatPause = 'paus',
    kWhatResume = 'rsme',
    kWhatReset = 'rset',

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

  std::shared_ptr<AvpDecoder> mAudioDecoder;
  std::shared_ptr<AvpDecoder> mVideoDecoder;
  std::shared_ptr<Looper> mPlayerLooper;

  std::shared_ptr<AudioSink> mAudioSink;
  std::shared_ptr<VideoSink> mVideoSink;
  std::shared_ptr<ContentSource> mSource;
  std::shared_ptr<AvpRenderSynchronizer> mRender;

  bool mStarted;
  bool mPrepared;
  bool mPaused;
  bool mSourceStarted;
  bool mScanSourcesPendding;
};

}  // namespace avp

#endif /* !AVP_AVPLAYER_H */
