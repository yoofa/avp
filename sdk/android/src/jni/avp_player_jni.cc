/*
 * avp_player_jni.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "sdk/android/src/jni/avp_player_jni.h"

#include <memory>
#include <string>
#include <unordered_map>

#include <android/native_window_jni.h>

#include "base/android/jni/jvm.h"
#include "base/logging.h"
#include "jni_headers/sdk/android/generated_avp_jni/AvpPlayer_jni.h"
#include "jni_headers/sdk/android/generated_avp_jni/TrackInfo_jni.h"
#include "media/android/jni/video_frame_jni.h"
#include "media/audio/android/java_audio_device.h"
#include "media/foundation/media_frame.h"
#include "media/foundation/media_meta.h"
#include "third_party/jni_zero/jni_zero.h"

namespace ave {
namespace jni {

namespace {

// Convert native MediaType to Java track type constant
int MediaTypeToTrackType(media::MediaType type) {
  switch (type) {
    case media::MediaType::VIDEO:
      return 0;  // MEDIA_TRACK_TYPE_VIDEO
    case media::MediaType::AUDIO:
      return 1;  // MEDIA_TRACK_TYPE_AUDIO
    case media::MediaType::SUBTITLE:
      return 2;  // MEDIA_TRACK_TYPE_SUBTITLE
    default:
      return -1;  // MEDIA_TRACK_TYPE_UNKNOWN
  }
}

}  // namespace

// --- AvpPlayerJni implementation ---

AvpPlayerJni::AvpPlayerJni(JNIEnv* env,
                           jobject j_player,
                           jobject j_audio_device,
                           jboolean sync_enabled,
                           jint audio_passthrough_policy,
                           jboolean audio_only)
    : j_player_(env, j_player) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::AvpPlayerJni created";

  // Create JavaAudioDevice from the Java AudioDevice object.
  player::Player::Builder builder;
  if (j_audio_device) {
    j_audio_device_ =
        jni_zero::ScopedJavaGlobalRef<jobject>(env, j_audio_device);
    java_audio_device_ =
        std::make_shared<media::android::JavaAudioDevice>(j_audio_device_);
    java_audio_device_->Init();
    builder.setAudioDevice(java_audio_device_);
    AVE_LOG(LS_INFO) << "AvpPlayerJni: using JavaAudioDevice";
  }
  builder.setSyncEnabled(sync_enabled)
      .setAudioPassthroughPolicy(
          static_cast<player::AudioPassthroughPolicy>(audio_passthrough_policy))
      .setAudioOnly(audio_only);

  player_ = builder.build();
  if (player_) {
    player_->Init();
    // Create a non-owning shared_ptr and store it as a member to keep
    // the strong reference alive. AvPlayer stores listener as weak_ptr,
    // so without this the weak_ptr would expire immediately.
    self_as_listener_ =
        std::shared_ptr<player::Player::Listener>(this, [](auto*) {});
    player_->SetListener(self_as_listener_);
    AVE_LOG(LS_INFO) << "AvpPlayerJni: player initialized, listener set";
  } else {
    AVE_LOG(LS_ERROR) << "AvpPlayerJni: failed to create player";
  }
}

AvpPlayerJni::~AvpPlayerJni() {
  if (player_) {
    player_->Stop();
    player_->Reset();
  }
  // native_window_render_ shared_ptr destructor releases the ANativeWindow.
  native_window_render_.reset();
}

void AvpPlayerJni::SetDataSource(JNIEnv* env, const std::string& path) {
  if (!player_)
    return;
  std::unordered_map<std::string, std::string> headers;
  player_->SetDataSource(path.c_str(), headers);
}

void AvpPlayerJni::SetDataSourceFd(JNIEnv* env,
                                   jint fd,
                                   jlong offset,
                                   jlong length) {
  if (!player_)
    return;
  player_->SetDataSource(fd, offset, length);
}

void AvpPlayerJni::SetVideoRenderer(JNIEnv* env, jboolean has_renderer) {
  has_video_renderer_ = has_renderer;
  if (!player_)
    return;
  if (has_renderer) {
    player_->SetVideoRender(
        std::shared_ptr<media::VideoRender>(this, [](auto*) {}));
  } else {
    player_->SetVideoRender(nullptr);
  }
}

void AvpPlayerJni::SetSurface(JNIEnv* env,
                              const jni_zero::JavaParamRef<jobject>& surface) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::SetSurface called";

  // Release existing surface render (decrements ANativeWindow ref count).
  native_window_render_.reset();

  if (surface.obj()) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface.obj());
    if (window) {
      native_window_render_ =
          std::make_shared<media::AndroidNativeWindowRender>(window);
      // AndroidNativeWindowRender acquired its own reference; release ours.
      ANativeWindow_release(window);
      AVE_LOG(LS_INFO) << "AvpPlayerJni::SetSurface: native_window_render="
                       << native_window_render_.get();
    }
  }

  if (!player_)
    return;

  if (native_window_render_) {
    player_->SetVideoRender(native_window_render_);
    AVE_LOG(LS_INFO) << "AvpPlayerJni::SetSurface: video render set";
  } else {
    player_->SetVideoRender(nullptr);
    AVE_LOG(LS_INFO) << "AvpPlayerJni::SetSurface: video render cleared";
  }
}

void AvpPlayerJni::Prepare(JNIEnv* env) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::Prepare called";
  if (!player_)
    return;
  player_->Prepare();
}

void AvpPlayerJni::Start(JNIEnv* env) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::Start called";
  if (!player_) {
    AVE_LOG(LS_ERROR) << "AvpPlayerJni::Start: player_ is null!";
    return;
  }
  player_->Start();
  AVE_LOG(LS_INFO) << "AvpPlayerJni::Start: player_->Start() returned";
}

void AvpPlayerJni::Pause(JNIEnv* env) {
  if (!player_)
    return;
  player_->Pause();
}

void AvpPlayerJni::Resume(JNIEnv* env) {
  if (!player_)
    return;
  player_->Resume();
}

void AvpPlayerJni::Stop(JNIEnv* env) {
  if (!player_)
    return;
  player_->Stop();
}

void AvpPlayerJni::SeekTo(JNIEnv* env, jint msec, jint mode) {
  if (!player_)
    return;
  player_->SeekTo(msec, static_cast<player::SeekMode>(mode));
}

void AvpPlayerJni::Reset(JNIEnv* env) {
  if (!player_)
    return;
  player_->Reset();
}

void AvpPlayerJni::Release(JNIEnv* env) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::Release: begin";
  if (player_) {
    // 1. Synchronously stop: renders halted, decoders shut down.
    player_->StopSync();
    // 2. Join the player looper thread from THIS (JNI) thread. This prevents
    //    ~AvPlayer() from running on the looper thread and self-joining.
    player_->PrepareDestroy();
    AVE_LOG(LS_INFO)
        << "AvpPlayerJni::Release: looper stopped, destroying player";
    // 3. Now safe to reset: ~AvPlayer() runs here on JNI thread; looper is
    //    already stopped so player_looper_->stop() in ~AvPlayer() is a no-op.
    player_.reset();
    AVE_LOG(LS_INFO) << "AvpPlayerJni::Release: player destroyed";
  }
  self_as_listener_.reset();
  native_window_render_.reset();
  AVE_LOG(LS_INFO) << "AvpPlayerJni::Release: complete";
}

jint AvpPlayerJni::GetDuration(JNIEnv* env) {
  if (!player_)
    return -1;
  int msec = 0;
  if (player_->GetDuration(&msec) != ave::OK) {
    return -1;
  }
  return msec;
}

jint AvpPlayerJni::GetCurrentPosition(JNIEnv* env) {
  if (!player_)
    return 0;
  int msec = 0;
  player_->GetCurrentPosition(&msec);
  return msec;
}

jboolean AvpPlayerJni::IsPlaying(JNIEnv* env) {
  if (!player_)
    return false;
  return player_->IsPlaying();
}

jint AvpPlayerJni::GetVideoWidth(JNIEnv* env) {
  if (!player_)
    return 0;
  return player_->GetVideoWidth();
}

jint AvpPlayerJni::GetVideoHeight(JNIEnv* env) {
  if (!player_)
    return 0;
  return player_->GetVideoHeight();
}

void AvpPlayerJni::SetPlaybackRate(JNIEnv* env, jfloat rate) {
  if (!player_)
    return;
  player_->SetPlaybackRate(rate);
}

jfloat AvpPlayerJni::GetPlaybackRate(JNIEnv* env) {
  if (!player_)
    return 1.0f;
  return player_->GetPlaybackRate();
}

void AvpPlayerJni::SetVolume(JNIEnv* env,
                             jfloat left_volume,
                             jfloat right_volume) {
  if (!player_)
    return;
  player_->SetVolume(left_volume, right_volume);
}

jint AvpPlayerJni::GetTrackCount(JNIEnv* env) {
  if (!player_)
    return 0;
  return static_cast<jint>(player_->GetTrackCount());
}

jni_zero::ScopedJavaLocalRef<jobject> AvpPlayerJni::GetTrackInfo(JNIEnv* env,
                                                                 jint index) {
  if (!player_)
    return jni_zero::ScopedJavaLocalRef<jobject>();

  auto meta = player_->GetTrackInfo(static_cast<size_t>(index));
  if (!meta)
    return jni_zero::ScopedJavaLocalRef<jobject>();

  int track_type = MediaTypeToTrackType(meta->stream_type());
  auto j_track_info = Java_TrackInfo_Constructor(env, track_type, index);

  if (j_track_info.obj()) {
    const std::string& mime = meta->mime();
    if (!mime.empty()) {
      Java_TrackInfo_setMimeType(env, j_track_info, mime);
    }
  }

  return j_track_info;
}

void AvpPlayerJni::SelectTrack(JNIEnv* env, jint index, jboolean select) {
  if (!player_)
    return;
  player_->SelectTrack(static_cast<size_t>(index), select);
}

// --- Player::Listener callbacks ---

void AvpPlayerJni::OnPrepared(status_t err) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::OnPrepared: err=" << err;
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env) {
    AVE_LOG(LS_ERROR) << "AvpPlayerJni::OnPrepared: failed to attach thread";
    return;
  }
  AVE_LOG(LS_VERBOSE) << "AvpPlayerJni::OnPrepared: calling Java callback"
                      << ", j_player_.obj()=" << j_player_.obj();
  Java_AvpPlayer_onNativePrepared(env, j_player_, static_cast<int>(err));
  AVE_LOG(LS_INFO) << "AvpPlayerJni::OnPrepared: Java callback returned";
}

void AvpPlayerJni::OnCompletion() {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::OnCompletion";
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeCompletion(env, j_player_);
}

void AvpPlayerJni::OnError(status_t error) {
  AVE_LOG(LS_ERROR) << "AvpPlayerJni::OnError: " << error;
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeError(env, j_player_, static_cast<int>(error));
}

void AvpPlayerJni::OnSeekComplete() {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::OnSeekComplete";
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeSeekComplete(env, j_player_);
}

void AvpPlayerJni::OnBufferingUpdate(int percent) {
  AVE_LOG(LS_VERBOSE) << "AvpPlayerJni::OnBufferingUpdate: " << percent;
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeBufferingUpdate(env, j_player_, percent);
}

void AvpPlayerJni::OnVideoSizeChanged(int width, int height) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::OnVideoSizeChanged: " << width << "x"
                   << height;
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeVideoSizeChanged(env, j_player_, width, height);
}

void AvpPlayerJni::OnInfo(int what, int extra) {
  AVE_LOG(LS_INFO) << "AvpPlayerJni::OnInfo: what=" << what
                   << ", extra=" << extra;
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeInfo(env, j_player_, what, extra);
}

void AvpPlayerJni::OnFrame(const std::shared_ptr<media::MediaFrame>& frame) {
  // This path is only reached when SetVideoRenderer() sets AvpPlayerJni as
  // the video render (no surface). Delegate to Java via VideoRenderer callback.
  if (!frame || !has_video_renderer_)
    return;

  auto* vinfo = frame->video_info();
  if (!vinfo || !frame->data())
    return;

  JNIEnv* env = AttachCurrentThreadIfNeeded();
  int width = vinfo->width;
  int height = vinfo->height;
  int stride = vinfo->stride > 0 ? vinfo->stride : width;
  int64_t timestamp_us = vinfo->pts.IsFinite() ? vinfo->pts.us() : 0;

  auto j_frame =
      media::jni::CreateJavaVideoFrame(env, width, height, stride, timestamp_us,
                                       0, frame->data(), frame->size());
  if (j_frame.obj()) {
    Java_AvpPlayer_onNativeVideoFrame(env, j_player_, j_frame);
  }
}

// Called by jni_zero auto-generated JNI_AvpPlayer_Init dispatch
static jlong JNI_AvpPlayer_Init(
    JNIEnv* env,
    const jni_zero::JavaParamRef<jobject>& j_caller,
    const jni_zero::JavaParamRef<jobject>& j_audio_device,
    jboolean sync_enabled,
    jint audio_passthrough_policy,
    jboolean audio_only) {
  auto* player =
      new AvpPlayerJni(env, j_caller.obj(), j_audio_device.obj(), sync_enabled,
                       audio_passthrough_policy, audio_only);
  return reinterpret_cast<jlong>(player);
}

}  // namespace jni
}  // namespace ave
