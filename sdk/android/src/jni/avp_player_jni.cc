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

#include "base/android/jni/jvm.h"
#include "base/logging.h"
#include "jni_headers/sdk/android/generated_avp_jni/AvpPlayer_jni.h"
#include "media/android/jni/video_frame_jni.h"
#include "media/foundation/media_frame.h"
#include "third_party/jni_zero/jni_zero.h"

namespace ave {
namespace jni {

// --- AvpPlayerJni implementation ---

AvpPlayerJni::AvpPlayerJni(JNIEnv* env, jobject j_player)
    : j_player_(env, j_player) {
  player_ = player::Player::Builder().build();
  if (player_) {
    player_->Init();
    player_->SetListener(
        std::shared_ptr<player::Player::Listener>(this, [](auto*) {}));
  }
}

AvpPlayerJni::~AvpPlayerJni() {
  if (player_) {
    player_->Stop();
    player_->Reset();
  }
}

void AvpPlayerJni::SetDataSource(
    JNIEnv* env, const jni_zero::JavaParamRef<jstring>& j_path) {
  if (!player_) return;
  const char* c_path = env->GetStringUTFChars(j_path.obj(), nullptr);
  std::unordered_map<std::string, std::string> headers;
  player_->SetDataSource(c_path, headers);
  env->ReleaseStringUTFChars(j_path.obj(), c_path);
}

void AvpPlayerJni::SetDataSourceFd(JNIEnv* env, jint fd, jlong offset,
                                    jlong length) {
  if (!player_) return;
  player_->SetDataSource(fd, offset, length);
}

void AvpPlayerJni::SetVideoRenderer(JNIEnv* env, jboolean has_renderer) {
  has_video_renderer_ = has_renderer;
  if (!player_) return;
  if (has_renderer) {
    player_->SetVideoRender(
        std::shared_ptr<media::VideoRender>(this, [](auto*) {}));
  } else {
    player_->SetVideoRender(nullptr);
  }
}

void AvpPlayerJni::SetSurface(
    JNIEnv* env, const jni_zero::JavaParamRef<jobject>& surface) {
  // TODO: Implement Surface-based rendering via ANativeWindow
}

void AvpPlayerJni::Prepare(JNIEnv* env) {
  if (!player_) return;
  player_->Prepare();
}

void AvpPlayerJni::Start(JNIEnv* env) {
  if (!player_) return;
  player_->Start();
}

void AvpPlayerJni::Pause(JNIEnv* env) {
  if (!player_) return;
  player_->Pause();
}

void AvpPlayerJni::Resume(JNIEnv* env) {
  if (!player_) return;
  player_->Resume();
}

void AvpPlayerJni::Stop(JNIEnv* env) {
  if (!player_) return;
  player_->Stop();
}

void AvpPlayerJni::SeekTo(JNIEnv* env, jint msec) {
  if (!player_) return;
  player_->SeekTo(msec);
}

void AvpPlayerJni::Reset(JNIEnv* env) {
  if (!player_) return;
  player_->Reset();
}

void AvpPlayerJni::Release(JNIEnv* env) {
  if (player_) {
    player_->Stop();
    player_->Reset();
    player_.reset();
  }
}

void AvpPlayerJni::OnCompletion() {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeCompletion(env, j_player_);
}

void AvpPlayerJni::OnPrepared() {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativePrepared(env, j_player_);
}

void AvpPlayerJni::OnError(status_t error) {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  Java_AvpPlayer_onNativeError(env, j_player_, static_cast<int>(error));
}

void AvpPlayerJni::OnFrame(const std::shared_ptr<media::MediaFrame>& frame) {
  if (!has_video_renderer_ || !frame) return;

  JNIEnv* env = AttachCurrentThreadIfNeeded();
  auto* vinfo = frame->video_info();
  if (!vinfo) return;

  int width = vinfo->width;
  int height = vinfo->height;
  int stride = vinfo->stride > 0 ? vinfo->stride : width;
  int64_t timestamp_us = vinfo->pts.IsFinite() ? vinfo->pts.us() : 0;

  auto j_frame = media::jni::CreateJavaVideoFrame(
      env, width, height, stride, timestamp_us, 0,
      frame->data(), frame->size());

  if (j_frame.obj()) {
    Java_AvpPlayer_onNativeVideoFrame(env, j_player_, j_frame);
  }
}

// Called by jni_zero auto-generated JNI_AvpPlayer_Init dispatch
static jlong JNI_AvpPlayer_Init(
    JNIEnv* env, const jni_zero::JavaParamRef<jobject>& j_caller) {
  auto* player = new AvpPlayerJni(env, j_caller.obj());
  return reinterpret_cast<jlong>(player);
}

}  // namespace jni
}  // namespace ave
