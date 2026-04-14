/*
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "sdk/android/src/jni/java_http_provider_jni.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "base/android/jni/jvm.h"
#include "base/logging.h"

namespace ave {
namespace jni {

namespace {

bool CheckAndClearJavaException(JNIEnv* env, const char* operation) {
  if (!env || !env->ExceptionCheck()) {
    return false;
  }
  env->ExceptionDescribe();
  env->ExceptionClear();
  AVE_LOG(LS_ERROR) << "Java HTTP bridge exception in " << operation;
  return true;
}

jclass GetStringClass(JNIEnv* env) {
  static jclass string_class = nullptr;
  if (!string_class) {
    jclass local = env->FindClass("java/lang/String");
    if (!local) {
      CheckAndClearJavaException(env, "FindClass(java/lang/String)");
      return nullptr;
    }
    string_class = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
  }
  return string_class;
}

jobjectArray CreateStringArray(JNIEnv* env,
                               const std::vector<std::string>& values) {
  jclass string_class = GetStringClass(env);
  if (!string_class) {
    return nullptr;
  }

  jobjectArray array = env->NewObjectArray(static_cast<jsize>(values.size()),
                                           string_class, nullptr);
  if (!array || CheckAndClearJavaException(env, "NewObjectArray")) {
    return nullptr;
  }

  for (size_t i = 0; i < values.size(); ++i) {
    jni_zero::ScopedJavaLocalRef<jstring> value(
        env, env->NewStringUTF(values[i].c_str()));
    if (!value.obj() || CheckAndClearJavaException(env, "NewStringUTF")) {
      return nullptr;
    }
    env->SetObjectArrayElement(array, static_cast<jsize>(i), value.obj());
    if (CheckAndClearJavaException(env, "SetObjectArrayElement")) {
      return nullptr;
    }
  }

  return array;
}

status_t ConvertJavaString(JNIEnv* env, jstring j_value, std::string& value) {
  value.clear();
  if (!j_value) {
    return OK;
  }

  const char* chars = env->GetStringUTFChars(j_value, nullptr);
  if (!chars || CheckAndClearJavaException(env, "GetStringUTFChars")) {
    return UNKNOWN_ERROR;
  }
  value.assign(chars);
  env->ReleaseStringUTFChars(j_value, chars);
  return OK;
}

}  // namespace

JavaHttpConnection::JavaHttpConnection(JNIEnv* env, jobject j_connection)
    : j_connection_(env, j_connection) {
  jclass cls = env->GetObjectClass(j_connection);
  connect_method_ = env->GetMethodID(
      cls, "connect",
      "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)Z");
  disconnect_method_ = env->GetMethodID(cls, "disconnect", "()V");
  read_at_method_ = env->GetMethodID(cls, "readAt", "(JI)[B");
  get_size_method_ = env->GetMethodID(cls, "getSize", "()J");
  get_mime_type_method_ =
      env->GetMethodID(cls, "getMimeType", "()Ljava/lang/String;");
  get_uri_method_ = env->GetMethodID(cls, "getUri", "()Ljava/lang/String;");
  env->DeleteLocalRef(cls);
}

bool JavaHttpConnection::Connect(
    const char* uri,
    const std::unordered_map<std::string, std::string>& headers) {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_connection_.obj() || !connect_method_) {
    return false;
  }

  std::vector<std::string> header_keys;
  std::vector<std::string> header_values;
  header_keys.reserve(headers.size());
  header_values.reserve(headers.size());
  for (const auto& [key, value] : headers) {
    header_keys.push_back(key);
    header_values.push_back(value);
  }

  jni_zero::ScopedJavaLocalRef<jstring> j_uri(
      env, env->NewStringUTF(uri ? uri : ""));
  jni_zero::ScopedJavaLocalRef<jobjectArray> j_keys(
      env, CreateStringArray(env, header_keys));
  jni_zero::ScopedJavaLocalRef<jobjectArray> j_values(
      env, CreateStringArray(env, header_values));
  if (!j_uri.obj() || CheckAndClearJavaException(env, "NewStringUTF(uri)")) {
    return false;
  }

  const jboolean connected =
      env->CallBooleanMethod(j_connection_.obj(), connect_method_, j_uri.obj(),
                             j_keys.obj(), j_values.obj());
  if (CheckAndClearJavaException(env, "HttpConnection.connect")) {
    return false;
  }
  return connected == JNI_TRUE;
}

void JavaHttpConnection::Disconnect() {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_connection_.obj() || !disconnect_method_) {
    return;
  }
  env->CallVoidMethod(j_connection_.obj(), disconnect_method_);
  CheckAndClearJavaException(env, "HttpConnection.disconnect");
}

ssize_t JavaHttpConnection::ReadAt(off64_t offset, void* data, size_t size) {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_connection_.obj() || !read_at_method_ || !data) {
    return -1;
  }

  const size_t clamped_size = std::min<size_t>(
      size, static_cast<size_t>(std::numeric_limits<jint>::max()));
  jni_zero::ScopedJavaLocalRef<jbyteArray> bytes(
      env, static_cast<jbyteArray>(env->CallObjectMethod(
               j_connection_.obj(), read_at_method_, static_cast<jlong>(offset),
               static_cast<jint>(clamped_size))));
  if (CheckAndClearJavaException(env, "HttpConnection.readAt")) {
    return -1;
  }
  if (!bytes.obj()) {
    return -1;
  }

  const jsize length = env->GetArrayLength(bytes.obj());
  if (length > 0) {
    env->GetByteArrayRegion(bytes.obj(), 0, length, static_cast<jbyte*>(data));
    if (CheckAndClearJavaException(env, "GetByteArrayRegion")) {
      return -1;
    }
  }
  return static_cast<ssize_t>(length);
}

off64_t JavaHttpConnection::GetSize() {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_connection_.obj() || !get_size_method_) {
    return -1;
  }
  const jlong size = env->CallLongMethod(j_connection_.obj(), get_size_method_);
  if (CheckAndClearJavaException(env, "HttpConnection.getSize")) {
    return -1;
  }
  return static_cast<off64_t>(size);
}

status_t JavaHttpConnection::GetMIMEType(std::string& mime_type) {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_connection_.obj() || !get_mime_type_method_) {
    return NO_INIT;
  }
  jni_zero::ScopedJavaLocalRef<jstring> j_mime(
      env, static_cast<jstring>(env->CallObjectMethod(j_connection_.obj(),
                                                      get_mime_type_method_)));
  if (CheckAndClearJavaException(env, "HttpConnection.getMimeType")) {
    return UNKNOWN_ERROR;
  }
  return ConvertJavaString(env, j_mime.obj(), mime_type);
}

status_t JavaHttpConnection::GetUri(std::string& uri) {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_connection_.obj() || !get_uri_method_) {
    return NO_INIT;
  }
  jni_zero::ScopedJavaLocalRef<jstring> j_uri(
      env, static_cast<jstring>(
               env->CallObjectMethod(j_connection_.obj(), get_uri_method_)));
  if (CheckAndClearJavaException(env, "HttpConnection.getUri")) {
    return UNKNOWN_ERROR;
  }
  return ConvertJavaString(env, j_uri.obj(), uri);
}

JavaHttpProvider::JavaHttpProvider(JNIEnv* env, jobject j_provider)
    : j_provider_(env, j_provider) {
  jclass cls = env->GetObjectClass(j_provider);
  create_connection_method_ = env->GetMethodID(
      cls, "createConnection", "()Lio/github/yoofa/avp/HttpConnection;");
  supports_scheme_method_ =
      env->GetMethodID(cls, "supportsScheme", "(Ljava/lang/String;)Z");
  env->DeleteLocalRef(cls);
}

std::shared_ptr<net::HTTPConnection> JavaHttpProvider::CreateConnection() {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_provider_.obj() || !create_connection_method_) {
    return nullptr;
  }

  jni_zero::ScopedJavaLocalRef<jobject> j_connection(
      env, env->CallObjectMethod(j_provider_.obj(), create_connection_method_));
  if (CheckAndClearJavaException(env, "HttpProvider.createConnection")) {
    return nullptr;
  }
  if (!j_connection.obj()) {
    return nullptr;
  }
  return std::make_shared<JavaHttpConnection>(env, j_connection.obj());
}

bool JavaHttpProvider::SupportsScheme(const std::string& scheme) {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  if (!env || !j_provider_.obj() || !supports_scheme_method_) {
    return false;
  }

  jni_zero::ScopedJavaLocalRef<jstring> j_scheme(
      env, env->NewStringUTF(scheme.c_str()));
  if (!j_scheme.obj() ||
      CheckAndClearJavaException(env, "NewStringUTF(scheme)")) {
    return false;
  }

  const jboolean supported = env->CallBooleanMethod(
      j_provider_.obj(), supports_scheme_method_, j_scheme.obj());
  if (CheckAndClearJavaException(env, "HttpProvider.supportsScheme")) {
    return false;
  }
  return supported == JNI_TRUE;
}

}  // namespace jni
}  // namespace ave
