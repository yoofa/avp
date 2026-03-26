/*
 * avp_player_jni.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef SDK_ANDROID_SRC_JNI_AVP_PLAYER_JNI_H_
#define SDK_ANDROID_SRC_JNI_AVP_PLAYER_JNI_H_

#include <jni.h>
#include <memory>

#include "api/player.h"
#include "media/codec/android/android_native_window_render.h"
#include "media/video/video_render.h"
#include "third_party/jni_zero/java_refs.h"

namespace ave {
namespace jni {

/**
 * @brief JNI wrapper that bridges Java AvpPlayer to native C++ Player.
 *        Holds the native Player instance and forwards Java calls.
 */
class AvpPlayerJni : public player::Player::Listener,
                     public media::VideoRender {
 public:
  AvpPlayerJni(JNIEnv* env, jobject j_player);
  ~AvpPlayerJni() override;

  // Player control methods (called from auto-generated JNI dispatch)
  void SetDataSource(JNIEnv* env, const jni_zero::JavaParamRef<jstring>& path);
  void SetDataSourceFd(JNIEnv* env, jint fd, jlong offset, jlong length);
  void SetVideoRenderer(JNIEnv* env, jboolean has_renderer);
  void SetSurface(JNIEnv* env, const jni_zero::JavaParamRef<jobject>& surface);
  void Prepare(JNIEnv* env);
  void Start(JNIEnv* env);
  void Pause(JNIEnv* env);
  void Resume(JNIEnv* env);
  void Stop(JNIEnv* env);
  void SeekTo(JNIEnv* env, jint msec, jint mode);
  void Reset(JNIEnv* env);
  void Release(JNIEnv* env);

  // Query methods
  jint GetDuration(JNIEnv* env);
  jint GetCurrentPosition(JNIEnv* env);
  jboolean IsPlaying(JNIEnv* env);
  jint GetVideoWidth(JNIEnv* env);
  jint GetVideoHeight(JNIEnv* env);

  // Playback rate
  void SetPlaybackRate(JNIEnv* env, jfloat rate);
  jfloat GetPlaybackRate(JNIEnv* env);

  // Volume
  void SetVolume(JNIEnv* env, jfloat left_volume, jfloat right_volume);

  // Track management
  jint GetTrackCount(JNIEnv* env);
  jni_zero::ScopedJavaLocalRef<jobject> GetTrackInfo(JNIEnv* env, jint index);
  void SelectTrack(JNIEnv* env, jint index, jboolean select);

  // Player::Listener
  void OnPrepared(status_t err) override;
  void OnCompletion() override;
  void OnError(status_t error) override;
  void OnSeekComplete() override;
  void OnBufferingUpdate(int percent) override;
  void OnVideoSizeChanged(int width, int height) override;
  void OnInfo(int what, int extra) override;

  // VideoRender (MediaFrameSink) — handles Java-callback path (no surface)
  void OnFrame(const std::shared_ptr<media::MediaFrame>& frame) override;

 private:
  jni_zero::ScopedJavaGlobalRef<jobject> j_player_;
  std::shared_ptr<player::Player> player_;
  // Must keep a strong reference to the listener shared_ptr so that
  // the weak_ptr stored inside AvPlayer doesn't expire immediately.
  std::shared_ptr<player::Player::Listener> self_as_listener_;
  // Surface-based rendering. Owned here; lifecycle managed via shared_ptr.
  std::shared_ptr<media::AndroidNativeWindowRender> native_window_render_;
  bool has_video_renderer_ = false;
};

}  // namespace jni
}  // namespace ave

#endif  // SDK_ANDROID_SRC_JNI_AVP_PLAYER_JNI_H_
