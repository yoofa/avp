/*
 * avplayer.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avplayer.h"

#include "base/byte_utils.h"
#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "base/task_util/default_task_runner_factory.h"

#include "media/foundation/media_errors.h"
#include "media/foundation/media_meta.h"
#include "media/foundation/message_object.h"

#include "content_source/generic_source.h"

#include "message_def.h"

namespace ave {
namespace player {

using ave::media::MessageObject;

AvPlayer::AvPlayer(std::shared_ptr<ContentSourceFactory> content_source_factory,
                   std::shared_ptr<DemuxerFactory> demuxer_factory,
                   std::shared_ptr<CodecFactory> codec_factory,
                   std::shared_ptr<AudioDevice> audio_device)
    : task_runner_factory_(ave::base::CreateDefaultTaskRunnerFactory()),
      content_source_factory_(std::move(content_source_factory)),
      demuxer_factory_(std::move(demuxer_factory)),
      codec_factory_(std::move(codec_factory)),
      audio_device_(std::move(audio_device)),
      player_looper_(std::make_shared<Looper>()),
      started_(false),
      pending_start_with_prepare_async_(false),
      prepared_(false),
      paused_(false),
      paused_for_buffering_(false),
      paused_by_client_(false),
      source_started_(false),
      scan_sources_pending_(false),
      audio_eos_(false),
      video_eos_(false),
      resetting_(false),
      resume_pending_(false),
      previous_seek_time_us_(0),
      scan_sources_generation_(0),
      poll_duration_generation_(0),
      flushing_audio_(NONE),
      flushing_video_(NONE) {
  player_looper_->setName("AvPlayer");
  ClearFlushComplete();
}

AvPlayer::~AvPlayer() {
  player_looper_->unregisterHandler(id());
  player_looper_->stop();
}

status_t AvPlayer::SetListener(std::shared_ptr<Listener> listener) {
  listener_ = std::move(listener);
  return ave::OK;
}

status_t AvPlayer::Init() {
  player_looper_->start();
  player_looper_->registerHandler(shared_from_this());

  return 0;
}

status_t AvPlayer::SetDataSource(
    const char* url,
    const std::unordered_map<std::string, std::string>& headers) {
  auto source = content_source_factory_->CreateContentSource(url, headers);
  if (source == nullptr) {
    return ave::UNKNOWN_ERROR;
  }
  return SetDataSource(source);
}

status_t AvPlayer::SetDataSource(int fd, int64_t offset, int64_t length) {
  auto source =
      content_source_factory_->CreateContentSource(fd, offset, length);
  if (source == nullptr) {
    return ave::UNKNOWN_ERROR;
  }
  return SetDataSource(source);
}
status_t AvPlayer::SetDataSource(std::shared_ptr<ave::DataSource> data_source) {
  auto source =
      content_source_factory_->CreateContentSource(std::move(data_source));
  if (source == nullptr) {
    return ave::UNKNOWN_ERROR;
  }
  return SetDataSource(source);
}

status_t AvPlayer::SetDataSource(std::shared_ptr<ContentSource> source) {
  auto msg = std::make_shared<Message>(kWhatSetDataSource, shared_from_this());
  source->SetNotify(this);

  msg->setObject(kContentSource,
                 std::static_pointer_cast<MessageObject>(std::move(source)));
  msg->post();
  return ave::OK;
}

status_t AvPlayer::SetVideoRender(std::shared_ptr<VideoRender> video_render) {
  auto msg = std::make_shared<Message>(kWhatSetVideoRender, shared_from_this());
  msg->setObject(kVideoRender, std::move(video_render));
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Prepare() {
  auto msg = std::make_shared<Message>(kWhatPrepare, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Start() {
  auto msg = std::make_shared<Message>(kWhatStart, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Stop() {
  auto msg = std::make_shared<Message>(kWhatStop, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Pause() {
  auto msg = std::make_shared<Message>(kWhatPause, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Resume() {
  auto msg = std::make_shared<Message>(kWhatResume, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::SeekTo(int msec, SeekMode seekMode) {
  auto msg = std::make_shared<Message>(kWhatSeek, shared_from_this());
  msg->setInt64(kSeekToUs, msec * 1000LL);
  msg->setInt32(kSeekMode, static_cast<int32_t>(seekMode));
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Reset() {
  auto msg = std::make_shared<Message>(kWhatReset, shared_from_this());
  msg->post(0);
  return ave::OK;
}

///////////////////////////////////////////

void AvPlayer::PerformSetVideoRender(
    std::shared_ptr<VideoRender> video_render) {
  AVE_LOG(LS_DEBUG) << "performSetVideoRender: " << video_render.get();

  if (video_render == nullptr) {
    video_render_sink_ = nullptr;
    if (video_decoder_) {
      video_decoder_->SetVideoRender(nullptr);
    }
    return;
  }

  video_render_sink_ = video_render;

  if (video_decoder_) {
    // Set the new render sink to the existing decoder
    video_decoder_->SetVideoRender(video_render);
  }
  if (video_render_) {
    // If no decoder, just set the render sink
    video_render_->SetSink(video_render_sink_);
  }
}

void AvPlayer::PostScanSources() {
  if (scan_sources_pending_) {
    return;
  }
  auto msg = std::make_shared<Message>(kWhatScanSources, shared_from_this());
  msg->setInt32(kGeneration, scan_sources_generation_);
  msg->post();
  scan_sources_pending_ = true;
}

status_t AvPlayer::InstantiateDecoder(
    bool audio,
    std::shared_ptr<AVPDecoderBase>& decoder) {
  if (decoder != nullptr) {
    return ave::OK;
  }

  auto format =
      source_->GetTrackInfo(audio ? MediaType::AUDIO : MediaType::VIDEO);

  if (format == nullptr) {
    return ave::UNKNOWN_ERROR;
  }

  std::string mime = format->mime();
  AVE_LOG(LS_INFO) << "instantiateDecoder, audio:" << audio
                   << ", mime: " << mime;

  if (audio) {
    auto notify =
        std::make_shared<Message>(kWhatAudioNotify, shared_from_this());
    audio_render_ = std::make_shared<AVPAudioRender>(task_runner_factory_.get(),
                                                     sync_controller_.get(),
                                                     audio_device_, true);
    decoder = AVPDecoderFactory::CreateDecoder(codec_factory_, notify, source_,
                                               audio_render_, format);
  } else {
    auto notify =
        std::make_shared<Message>(kWhatVideoNotify, shared_from_this());
    video_render_ = std::make_shared<AVPVideoRender>(task_runner_factory_.get(),
                                                     sync_controller_.get());
    video_render_->SetSink(video_render_sink_);
    decoder = AVPDecoderFactory::CreateDecoder(codec_factory_, notify, source_,
                                               video_render_, format,
                                               video_render_sink_);
  }

  AVE_CHECK(decoder != nullptr)
      << "failed to create decoder for mime: " << mime;

  decoder->Init();

  decoder->Configure(format);

  decoder->Start();

  return ave::OK;
}

///////////////////////////////////////////

void AvPlayer::OnStart(int64_t start_us, SeekMode seek_mode) {
  if (!prepared_) {
    AVE_LOG(LS_INFO)
        << "start called before prepared, will start after prepared";
    // If not prepared yet, we will start after prepared
    pending_start_with_prepare_async_ = true;
    return;
  }
  AVE_LOG(LS_VERBOSE) << "onStart, start_us: " << start_us
                      << ", seek_mode: " << seek_mode;
  if (!source_started_) {
    source_->Start();
    source_started_ = true;
  }

  audio_eos_ = false;
  video_eos_ = false;
  started_ = true;
  paused_ = false;

  // Create sync controller if needed
  if (!sync_controller_) {
    sync_controller_ = std::make_shared<AVSyncControllerImpl>();
  }

  // Instantiate decoders immediately based on current track info (NuPlayer
  // style)
  bool has_audio = (source_->GetTrackInfo(MediaType::AUDIO) != nullptr);
  bool has_video = (source_->GetTrackInfo(MediaType::VIDEO) != nullptr);

  if (!has_audio && !has_video) {
    AVE_LOG(LS_ERROR) << "no metadata for either audio or video source";
    source_->Stop();
    source_started_ = false;
    if (listener_.lock()) {
      listener_.lock()->OnError(ave::UNKNOWN_ERROR);
    }
    return;
  }

  if (has_video && video_decoder_ == nullptr && video_render_sink_ != nullptr) {
    InstantiateDecoder(false /* audio */, video_decoder_);
  }
  if (has_audio && audio_decoder_ == nullptr && audio_device_ != nullptr) {
    InstantiateDecoder(true /* audio */, audio_decoder_);
  }

  // Start renders to accept frames (ExoPlayer style render lifecycle)
  if (audio_render_) {
    audio_render_->Start();
  }
  if (video_render_) {
    video_render_->Start();
  }

  // Apply initial seek if requested
  if (start_us > 0) {
    PerformSeek(start_us, seek_mode);
  }

  // TODO(youfa): dynamic duration
  // Schedule duration polling if needed
  // if (source_->HasDynamicDuration()) {
  //  SchedulePollDuration();
  //}

  // In case tracks/sinks appear later, keep scanning
  PostScanSources();
}

void AvPlayer::OnStop() {
  if (!started_) {
    return;
  }
  started_ = false;
  paused_ = false;
  if (audio_decoder_) {
    audio_decoder_->Shutdown();
    audio_decoder_.reset();
  }
  if (video_decoder_) {
    video_decoder_->Shutdown();
    video_decoder_.reset();
  }
  if (audio_render_) {
    audio_render_->Stop();
  }
  if (video_render_) {
    video_render_->Stop();
  }
  if (source_) {
    source_->Stop();
  }
}

void AvPlayer::OnPause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  if (source_) {
    source_->Pause();
  } else {
    AVE_LOG(LS_WARNING) << "pause called when source is gone or not set";
  }

  if (audio_decoder_) {
    audio_decoder_->Pause();
  }
  if (video_decoder_) {
    video_decoder_->Pause();
  }
  if (audio_render_) {
    audio_render_->Pause();
  }
  if (video_render_) {
    video_render_->Pause();
  }
}

void AvPlayer::OnResume() {
  if (!paused_) {
    return;
  }
  paused_ = false;
  if (audio_decoder_) {
    audio_decoder_->Resume();
  }
  if (video_decoder_) {
    video_decoder_->Resume();
  }
  if (audio_render_) {
    audio_render_->Resume();
  }
  if (video_render_) {
    video_render_->Resume();
  }
}

void AvPlayer::OnSeek(int64_t seek_to_us, SeekMode seek_mode) {
  if (source_) {
    PerformSeek(seek_to_us, seek_mode);
  }
  if (audio_decoder_) {
    audio_decoder_->Flush();
  }
  if (video_decoder_) {
    video_decoder_->Flush();
  }
  if (audio_render_) {
    audio_render_->Flush();
  }
  if (video_render_) {
    video_render_->Flush();
  }
}

void AvPlayer::PerformReset() {
  source_.reset();
}

void AvPlayer::ProcessDeferredActions() {
  while (!deferred_actions_.empty()) {
    // We won't execute any deferred actions until we're no longer in
    // an intermediate state, i.e. one more more decoders are currently
    // flushing or shutting down.
    if (flushing_audio_ != NONE || flushing_video_ != NONE) {
      // We're currently flushing, postpone the actions until that's completed.
      AVE_LOG(LS_DEBUG) << "postponing action flushing_audio="
                        << flushing_audio_
                        << ", flushing_video=" << flushing_video_;
      break;
    }

    auto action = deferred_actions_.front();
    deferred_actions_.erase(deferred_actions_.begin());
    action->Execute(this);
  }
}

void AvPlayer::PerformSeek(int64_t seek_time_us, SeekMode seek_mode) {
  AVE_LOG(LS_VERBOSE) << "performSeek seek_time_us=" << seek_time_us
                      << " us, mode=" << seek_mode;

  if (source_ == nullptr) {
    AVE_LOG(LS_ERROR) << "source is null during seek";
    return;
  }

  previous_seek_time_us_ = seek_time_us;
  source_->SeekTo(seek_time_us, seek_mode);
}

void AvPlayer::PerformDecoderFlush(FlushCommand audio, FlushCommand video) {
  AVE_LOG(LS_DEBUG) << "performDecoderFlush audio=" << audio
                    << ", video=" << video;

  if ((audio == FLUSH_CMD_NONE || audio_decoder_ == nullptr) &&
      (video == FLUSH_CMD_NONE || video_decoder_ == nullptr)) {
    return;
  }

  if (audio != FLUSH_CMD_NONE && audio_decoder_ != nullptr) {
    FlushDecoder(true /* audio */, (audio == FLUSH_CMD_SHUTDOWN));
  }

  if (video != FLUSH_CMD_NONE && video_decoder_ != nullptr) {
    FlushDecoder(false /* audio */, (video == FLUSH_CMD_SHUTDOWN));
  }
}

void AvPlayer::PerformScanSources() {
  AVE_LOG(LS_DEBUG) << "performScanSources";

  if (!started_) {
    return;
  }

  if (audio_decoder_ == nullptr || video_decoder_ == nullptr) {
    PostScanSources();
  }
}

void AvPlayer::PerformResumeDecoders(bool need_notify) {
  if (need_notify) {
    resume_pending_ = true;
    if (video_decoder_ == nullptr) {
      // if audio-only, we can notify seek complete now
      FinishResume();
    }
  }

  if (video_decoder_ != nullptr) {
    // TODO: Implement signalResume in AVPDecoder
    // video_decoder_->signalResume(need_notify);
  }

  if (audio_decoder_ != nullptr) {
    // TODO: Implement signalResume in AVPDecoder
    // audio_decoder_->signalResume(false /* need_notify */);
  }
}

void AvPlayer::FinishResume() {
  if (resume_pending_) {
    resume_pending_ = false;
    NotifyDriverSeekComplete();
  }
}

void AvPlayer::NotifyDriverSeekComplete() {
  if (listener_.lock()) {
    // TODO: Implement seek complete notification in Listener
    // listener_.lock()->onSeekComplete();
  }
}

void AvPlayer::HandleFlushComplete(bool audio, bool is_decoder) {
  // We wait for both the decoder flush and the renderer flush to complete
  // before entering either the FLUSHED or the SHUTTING_DOWN_DECODER state.
  flush_complete_[audio][is_decoder] = true;

  if (!flush_complete_[audio][!is_decoder]) {
    return;
  }

  FlushStatus* state = audio ? &flushing_audio_ : &flushing_video_;
  switch (*state) {
    case FLUSHING_DECODER:
      *state = FLUSHED;
      break;
    case FLUSHING_DECODER_SHUTDOWN:
      *state = SHUTTING_DOWN_DECODER;
      AVE_LOG(LS_DEBUG) << "initiating " << (audio ? "audio" : "video")
                        << " decoder shutdown";
      // TODO: Implement initiateShutdown in AVPDecoder
      // getDecoder(audio)->initiateShutdown();
      break;
    default:
      // decoder flush completes only occur in a flushing state.
      AVE_CHECK(!is_decoder) << "decoder flush in invalid state " << *state;
      break;
  }
}

void AvPlayer::FinishFlushIfPossible() {
  if (flushing_audio_ != NONE && flushing_audio_ != FLUSHED &&
      flushing_audio_ != SHUT_DOWN) {
    return;
  }

  if (flushing_video_ != NONE && flushing_video_ != FLUSHED &&
      flushing_video_ != SHUT_DOWN) {
    return;
  }

  AVE_LOG(LS_DEBUG) << "both audio and video are flushed now.";

  flushing_audio_ = NONE;
  flushing_video_ = NONE;

  ClearFlushComplete();

  ProcessDeferredActions();
}

void AvPlayer::ClearFlushComplete() {
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      flush_complete_[i][j] = false;
    }
  }
}

void AvPlayer::FlushDecoder(bool audio, bool need_shutdown) {
  AVE_LOG(LS_DEBUG) << "flushDecoder " << (audio ? "audio" : "video")
                    << " needShutdown=" << need_shutdown;

  auto& decoder = audio ? audio_decoder_ : video_decoder_;
  if (decoder == nullptr) {
    AVE_LOG(LS_INFO) << "flushDecoder " << (audio ? "audio" : "video")
                     << " without decoder present";
    return;
  }

  // Make sure we don't continue to scan sources until we finish flushing.
  ++scan_sources_generation_;
  if (scan_sources_pending_) {
    scan_sources_pending_ = false;
  }

  // TODO: Implement signalFlush in AVPDecoder
  // decoder->signalFlush();

  FlushStatus new_status =
      need_shutdown ? FLUSHING_DECODER_SHUTDOWN : FLUSHING_DECODER;

  flush_complete_[audio][false /* isDecoder */] =
      true;  // Renderer flush is immediate
  flush_complete_[audio][true /* isDecoder */] = false;

  if (audio) {
    AVE_CHECK(flushing_audio_ == NONE)
        << "audio flushDecoder() is called in state " << flushing_audio_;
    flushing_audio_ = new_status;
  } else {
    AVE_CHECK(flushing_video_ == NONE)
        << "video flushDecoder() is called in state " << flushing_video_;
    flushing_video_ = new_status;
  }
}

void AvPlayer::CancelPollDuration() {
  ++poll_duration_generation_;
}

void AvPlayer::SchedulePollDuration() {
  // TODO: Implement duration polling if needed
  AVE_LOG(LS_DEBUG) << "schedulePollDuration";
}

/********************* ContentSource::Notify Start *********************/
void AvPlayer::OnPrepared(status_t err) {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourcePrepared);
  msg->setInt32(kError, err);
  msg->post();
}

void AvPlayer::OnFlagsChanged(int32_t flags) {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceFlagsChanged);
  msg->setInt32(kFlags, flags);
  msg->post();
}

void AvPlayer::OnVideoSizeChanged(std::shared_ptr<MediaMeta>& format) {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceVideoSizeChanged);
  msg->setObject(kMediaMeta, std::static_pointer_cast<MessageObject>(format));
  msg->post();
}

void AvPlayer::OnSeekComplete() {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceSeekComplete);
  msg->post();
}

void AvPlayer::OnBufferingStart() {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceBufferingStart);
  msg->post();
}

void AvPlayer::OnBufferingUpdate(int percent) {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceBufferingUpdate);
  msg->setInt32(kPercent, percent);
  msg->post();
}

void AvPlayer::OnBufferingEnd() {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceBufferingEnd);
  msg->post();
}

void AvPlayer::OnCompletion() {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceCompletion);
  msg->post();
}

void AvPlayer::OnError(status_t error) {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceError);
  msg->setInt32(kError, error);
  msg->post();
}

void AvPlayer::OnFetchData(MediaType stream_type) {
  auto msg = std::make_shared<Message>(kWhatSourceNotify, shared_from_this());
  msg->setInt32(kWhat, kWhatSourceFetchData);
  msg->setInt32(kMediaType, static_cast<int32_t>(stream_type));
  msg->post();
}

/********************* ContentSource::Notify End *********************/

void AvPlayer::OnSourceNotify(const std::shared_ptr<Message>& msg) {
  int32_t what = 0;
  AVE_CHECK(msg->findInt32(kWhat, &what));
  switch (what) {
    case kWhatSourcePrepared: {
      AVE_LOG(LS_INFO) << "source prepared: " << source_;
      if (source_ == nullptr) {
        return;
      }
      status_t err = ave::OK;
      AVE_CHECK(msg->findInt32(kError, &err));
      if (err != ave::OK) {
        if (listener_.lock())
          listener_.lock()->OnError(err);
      } else {
        prepared_ = true;
      }

      // TODO(youfa) new msg here
      // notifyListner(kWhatPrepared, msg);

      if (pending_start_with_prepare_async_) {
        pending_start_with_prepare_async_ = false;
        OnStart();
      }
      break;
    }
    case kWhatSourceFlagsChanged: {
      int32_t flags = 0;
      msg->findInt32(kFlags, &flags);
      AVE_LOG(LS_DEBUG) << "flags changed: " << flags;
      break;
    }
    case kWhatSourceVideoSizeChanged: {
      std::shared_ptr<MessageObject> obj;
      if (msg->findObject(kMediaMeta, obj)) {
        auto format = std::dynamic_pointer_cast<MediaMeta>(obj);
        if (listener_.lock()) {
          // listener_.lock()->onVideoSizeChanged(format->width(),
          // format->height());
        }
      }
      break;
    }
    case kWhatSourceBufferingStart: {
      paused_for_buffering_ = true;
      if (audio_decoder_)
        audio_decoder_->Pause();
      if (video_decoder_)
        video_decoder_->Pause();
      if (audio_render_)
        audio_render_->Pause();
      if (video_render_)
        video_render_->Pause();
      break;
    }
    case kWhatSourceBufferingEnd: {
      paused_for_buffering_ = false;
      if (!paused_ && !paused_by_client_) {
        if (audio_decoder_)
          audio_decoder_->Resume();
        if (video_decoder_)
          video_decoder_->Resume();
        if (audio_render_)
          audio_render_->Resume();
        if (video_render_)
          video_render_->Resume();
      }
      break;
    }
    case kWhatSourceCompletion: {
      if (listener_.lock())
        listener_.lock()->OnCompletion();
      break;
    }
    case kWhatSourceError: {
      status_t err = ave::UNKNOWN_ERROR;
      msg->findInt32(kError, &err);
      if (listener_.lock()) {
        listener_.lock()->OnError(err);
      }
      break;
    }
    case kWhatSourceFetchData: {
      // Trigger scanning; decoders will pull on their own timers
      PostScanSources();
      break;
    }
    default:
      break;
  }
}

void AvPlayer::OnDecoderNotify(const std::shared_ptr<Message>& msg) {
  int32_t what = 0;
  AVE_CHECK(msg->findInt32(kWhat, &what));
  switch (what) {
    case AVPDecoder::kWhatVideoSizeChanged: {
      std::shared_ptr<MessageObject> obj;
      msg->findObject(kMediaMeta, obj);
      auto format = std::dynamic_pointer_cast<MediaMeta>(obj);
      if (listener_.lock()) {
        // TODO: Implement onVideoSizeChanged in Listener interface
        // listener_.lock()->onVideoSizeChanged(format->width(),
        // format->height());
      }
      break;
    }
    case AVPDecoder::kWhatAudioOutputFormatChanged: {
      // TODO: notify listener
      break;
    }
    case AVPDecoder::kWhatEOS: {
      int32_t is_audio = 0;
      msg->findInt32("is_audio", &is_audio);
      if (is_audio) {
        audio_eos_ = true;
      } else {
        video_eos_ = true;
      }

      if (audio_eos_ && video_eos_) {
        if (listener_.lock()) {
          listener_.lock()->OnCompletion();
        }
      }
      break;
    }
    case AVPDecoder::kWhatError: {
      status_t err = UNKNOWN_ERROR;
      msg->findInt32(kError, &err);
      if (listener_.lock()) {
        listener_.lock()->OnError(err);
      }
      break;
    }
    default:
      break;
  }
}

void AvPlayer::OnRenderNotify(const std::shared_ptr<Message>& msg) {}

void AvPlayer::onMessageReceived(const std::shared_ptr<Message>& message) {
  // make message->what() to 4cc
  char what_str[5] = {0};
  MakeFourCCString(message->what(), what_str);
  AVE_LOG(LS_VERBOSE) << "AvPlayer::onMessageReceived:" << what_str;
  std::lock_guard<std::mutex> l(mutex_);
  status_t err = ave::OK;
  switch (message->what()) {
      /************* from avplayer ***************/
    case kWhatSetDataSource: {
      AVE_LOG(LS_VERBOSE) << "kWhatSetDataSource";
      AVE_CHECK(source_ == nullptr)
          << "SetDataSource called when source is already set";
      std::shared_ptr<MessageObject> obj;
      AVE_CHECK(message->findObject(kContentSource, obj));
      if (obj != nullptr) {
        AVE_LOG(LS_INFO) << "set content source: " << obj.get();
        // TODO(youfa): maybe need source lock
        source_ = std::dynamic_pointer_cast<ContentSource>(obj);
      } else {
        AVE_LOG(LS_ERROR) << "no content source found in message";
        err = ave::UNKNOWN_ERROR;
      }
      // TODO(youfa): notify listener set data source completed
      break;
    }

    case kWhatSetVideoRender: {
      std::shared_ptr<MessageObject> obj;
      message->findObject(kVideoRender, obj);
      std::shared_ptr<VideoRender> video_render =
          std::dynamic_pointer_cast<VideoRender>(obj);
      AVE_LOG(LS_INFO) << "kWhatSetVideoRender (" << video_render_sink_.get()
                       << ", "
                       << ((started_ && source_ != nullptr &&
                            source_->GetTrackInfo(MediaType::VIDEO) !=
                                nullptr) &&
                                   video_decoder_ != nullptr
                               ? "has"
                               : "no")
                       << "video decoder=" << video_decoder_.get() << ")";

      if (source_ == nullptr || !started_ ||
          source_->GetTrackInfo(MediaType::VIDEO) == nullptr ||
          (video_decoder_ != nullptr &&
           video_decoder_->SetVideoRender(video_render) == ave::OK)) {
        PerformSetVideoRender(video_render);
        break;
      }

      deferred_actions_.push_back(std::make_shared<FlushDecoderAction>(
          video_render ? FLUSH_CMD_FLUSH : FLUSH_CMD_NONE /* audio */,
          FLUSH_CMD_FLUSH /* video */));
      // set video render action
      deferred_actions_.push_back(
          std::make_shared<SetVideoRenderSinkAction>(video_render));
      // deferred_actions_.push_back(std::make_shared<SimpleAction>(
      //     &AvPlayer::PerformSetVideoRender, video_render));

      if (video_render) {
        if (started_) {
          // int64_t current_time_us = 0;
          //  TODO(youfa): do seek
          //  if (GetCurrentPositionUs(current_time_us) == ave::OK) {
          //   deferred_actions_.push_back(std::make_shared<SeekAction>(
          //       current_time_us, SeekMode::SEEK_PREVIOUS_SYNC));
          // }
        }
        // scan sources and resume decoder
        deferred_actions_.push_back(
            std::make_shared<SimpleAction>(&AvPlayer::PerformScanSources));

        deferred_actions_.push_back(
            std::make_shared<ResumeDecoderAction>(true /* need_notify */));
      }

      ProcessDeferredActions();

      break;
    }

    case kWhatPrepare: {
      AVE_CHECK(source_);
      AVE_LOG(LS_VERBOSE) << "kWhatPrepare";
      source_->Prepare();
      break;
    }

    case kWhatStart: {
      AVE_LOG(LS_VERBOSE) << "kWhatStart";
      if (started_) {
        // do not resume yet if the source is still buffering
        if (!paused_for_buffering_) {
          OnResume();
        }
      } else {
        OnStart();
      }
      paused_by_client_ = false;
      break;
    }

    case kWhatStop: {
      OnStop();
      break;
    }

    case kWhatScanSources: {
      int32_t generation = 0;
      AVE_CHECK(message->findInt32(kGeneration, &generation));
      if (generation != scan_sources_generation_) {
        AVE_LOG(LS_DEBUG) << "skipping scanSources, generation mismatch "
                          << generation << " vs " << scan_sources_generation_;
        return;
      }

      scan_sources_pending_ = false;

      AVE_LOG(LS_INFO) << "scaning sources, has audio: "
                       << (audio_decoder_ == nullptr)
                       << ", has video: " << (video_decoder_ == nullptr);

      bool rescan = false;
      if (video_render_sink_ != nullptr && video_decoder_ == nullptr) {
        if (InstantiateDecoder(false, video_decoder_) == WOULD_BLOCK) {
          rescan = true;
        }
      }

      if (audio_device_ != nullptr && audio_decoder_ == nullptr) {
        if (InstantiateDecoder(true, audio_decoder_) == WOULD_BLOCK) {
          rescan = true;
        }
      }

      // try to feed some data
      if ((err = source_->FeedMoreESData()) != OK) {
        if (audio_decoder_ == nullptr && video_decoder_ == nullptr) {
          if (err == media::ERROR_END_OF_STREAM) {
            // TODO(youfa): notify listener end of stream

          } else {
            // TODO(youfa): notify listener error
          }
        }
        // do nothing
        break;
      }

      if (rescan) {
        message->post(1000000LL);
        scan_sources_pending_ = true;
      }
      break;
    }

    case kWhatSeek: {
      int64_t seek_to_us = 0;
      message->findInt64(kSeekToUs, &seek_to_us);
      int32_t seek_mode = 0;
      message->findInt32(kSeekMode, &seek_mode);

      AVE_LOG(LS_VERBOSE) << "kWhatSeek seek_to_us=" << seek_to_us
                          << " us, mode=" << seek_mode;

      if (!started_) {
        // Seek before the player is started. In order to preview video,
        // need to start the player and pause it.
        OnStart(seek_to_us, static_cast<SeekMode>(seek_mode));
        if (started_) {
          OnPause();
          paused_by_client_ = true;
        }
        NotifyDriverSeekComplete();
        break;
      }

      // Use deferred actions for proper state management
      deferred_actions_.push_back(std::make_shared<FlushDecoderAction>(
          FLUSH_CMD_FLUSH /* audio */, FLUSH_CMD_FLUSH /* video */));

      deferred_actions_.push_back(std::make_shared<SeekAction>(
          seek_to_us, static_cast<SeekMode>(seek_mode)));

      // After a flush without shutdown, decoder is paused.
      // Don't resume it until source seek is done, otherwise it could
      // start pulling stale data too soon.
      deferred_actions_.push_back(
          std::make_shared<ResumeDecoderAction>(true /* need_notify */));

      ProcessDeferredActions();
      break;
    }

    case kWhatPause: {
      OnPause();
      paused_by_client_ = true;
      break;
    }

    case kWhatResume: {
      OnResume();
      break;
    }

    case kWhatReset: {
      AVE_LOG(LS_DEBUG) << "kWhatReset";

      resetting_ = true;

      // Use deferred actions for proper state management
      deferred_actions_.push_back(std::make_shared<FlushDecoderAction>(
          FLUSH_CMD_SHUTDOWN /* audio */, FLUSH_CMD_SHUTDOWN /* video */));

      deferred_actions_.push_back(
          std::make_shared<SimpleAction>(&AvPlayer::PerformReset));

      ProcessDeferredActions();
      break;
    }

    case kWhatSourceNotify: {
      OnSourceNotify(message);
      break;
    }
    case kWhatAudioNotify:
    case kWhatVideoNotify: {
      OnDecoderNotify(message);
      break;
    }

    case kWhatRendererNotify: {
      OnRenderNotify(message);
      break;
    }
    default:
      break;
  }
}

// Action implementations
void AvPlayer::SeekAction::Execute(AvPlayer* player) {
  player->PerformSeek(seek_time_us_, seek_mode_);
}

void AvPlayer::ResumeDecoderAction::Execute(AvPlayer* player) {
  player->PerformResumeDecoders(need_notify_);
}

void AvPlayer::FlushDecoderAction::Execute(AvPlayer* player) {
  player->PerformDecoderFlush(audio_, video_);
}

void AvPlayer::SetVideoRenderSinkAction::Execute(AvPlayer* player) {
  player->PerformSetVideoRender(video_render_);
}

void AvPlayer::SimpleAction::Execute(AvPlayer* player) {
  (player->*func_)();
}

}  // namespace player
}  // namespace ave
