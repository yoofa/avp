/*
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef SDK_ANDROID_SRC_JNI_JAVA_HTTP_PROVIDER_JNI_H_
#define SDK_ANDROID_SRC_JNI_JAVA_HTTP_PROVIDER_JNI_H_

#include <jni.h>
#include <memory>
#include <string>

#include "base/net/http/http_provider.h"
#include "third_party/jni_zero/java_refs.h"

namespace ave {
namespace jni {

class JavaHttpConnection : public net::HTTPConnection {
 public:
  JavaHttpConnection(JNIEnv* env, jobject j_connection);
  ~JavaHttpConnection() override = default;

  bool Connect(
      const char* uri,
      const std::unordered_map<std::string, std::string>& headers) override;
  void Disconnect() override;
  ssize_t ReadAt(off64_t offset, void* data, size_t size) override;
  off64_t GetSize() override;
  status_t GetMIMEType(std::string& mime_type) override;
  status_t GetUri(std::string& uri) override;

 private:
  jni_zero::ScopedJavaGlobalRef<jobject> j_connection_;
  jmethodID connect_method_ = nullptr;
  jmethodID disconnect_method_ = nullptr;
  jmethodID read_at_method_ = nullptr;
  jmethodID get_size_method_ = nullptr;
  jmethodID get_mime_type_method_ = nullptr;
  jmethodID get_uri_method_ = nullptr;
};

class JavaHttpProvider : public net::HTTPProvider {
 public:
  JavaHttpProvider(JNIEnv* env, jobject j_provider);
  ~JavaHttpProvider() override = default;

  std::shared_ptr<net::HTTPConnection> CreateConnection() override;
  bool SupportsScheme(const std::string& scheme) override;

 private:
  jni_zero::ScopedJavaGlobalRef<jobject> j_provider_;
  jmethodID create_connection_method_ = nullptr;
  jmethodID supports_scheme_method_ = nullptr;
};

}  // namespace jni
}  // namespace ave

#endif  // SDK_ANDROID_SRC_JNI_JAVA_HTTP_PROVIDER_JNI_H_
