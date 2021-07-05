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

#include "player_interface.h"

namespace avp {
class AvPlayer : public PlayerBase, public Handler {
 public:
  AvPlayer();
  virtual ~AvPlayer();
  virtual status_t init() override;
  virtual status_t setDataSource(int fd,
                                 int64_t offset,
                                 int64_t length) override;
  virtual status_t prepare() override;
  virtual status_t start() override;
  virtual status_t stop() override;
  virtual status_t pause() override;
  virtual status_t resume() override;
  virtual status_t seekTo(
      int msec,
      SeekMode mode = SeekMode::SEEK_PREVIOUS_SYNC) override;
  virtual status_t reset() override;

 protected:
  void onMessageReceived(const std::shared_ptr<Message>& message) override;

 private:
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

  std::shared_ptr<Looper> mPlayerLooper;
};

}  // namespace avp

#endif /* !AVP_AVPLAYER_H */
