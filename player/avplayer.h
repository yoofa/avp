/*
 * avplayer.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_AVPLAYER_H
#define AVP_AVPLAYER_H

#include <memory>

#include "base/system/ave_export.h"

#include "media/audio/audio_device_factory.h"
#include "media/codec/codec_factory.h"

#include "media/foundation/handler.h"
#include "media/foundation/looper.h"
#include "media/foundation/message.h"

#include "api/player.h"

#include "player/audio_decoder.h"
#include "player/audio_decoder_factory.h"
#include "player/avp_decoder.h"
#include "player/avp_render_synchronizer.h"
#include "player/default_Audio_decoder_factory.h"
#include "player/default_video_decoder_factory.h"
#include "player/media_clock.h"
#include "player/player_interface.h"
#include "player/video_decoder.h"
#include "player/video_decoder_factory.h"
#include "player/video_sink.h"

namespace avp {

AVE_EXPORT class AvPlayer : public Player, public ave::Handler {
 public:
  AvPlayer();
  ~AvPlayer() override;

  status_t SetListener(const std::shared_ptr<Listener>& listener) override;
  status_t Init() override;
  status_t SetDataSource(const char* url) override;
  status_t SetDataSource(int fd, int64_t offset, int64_t length) override;
  status_t SetDataSource(const std::shared_ptr<ContentSource>& source) override;

  status_t Prepare() override;
  status_t Start() override;
  status_t Stop() override;
  status_t Pause() override;
  status_t Resume() override;
  status_t SeekTo(int msec, SeekMode mode) override;
  status_t Reset() override;

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

  std::shared_ptr<MediaClock> mMediaClock;
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
