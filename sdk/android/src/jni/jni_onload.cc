/*
 * jni_onload.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include <jni.h>

#include "base/android/jni/class_loader.h"
#include "base/android/jni/jvm.h"
#include "base/logging.h"

#undef JNIEXPORT
#define JNIEXPORT __attribute__((visibility("default")))

namespace ave {
namespace jni {

extern "C" jint JNIEXPORT JNICALL JNI_OnLoad(JavaVM* jvm, void* reserved) {
  AVE_LOG(LS_INFO) << "AVP JNI_OnLoad";
  jint ret = InitGlobalJniVariables(jvm);
  if (ret < 0) {
    return -1;
  }

  InitClassLoader(GetEnv());

  return ret;
}

extern "C" void JNIEXPORT JNICALL JNI_OnUnload(JavaVM* jvm, void* reserved) {
  AVE_LOG(LS_INFO) << "AVP JNI_OnUnload";
}

}  // namespace jni
}  // namespace ave
