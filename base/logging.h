/*
 * logging.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

#include "base/constructor_magic.h"

#if defined(AVP_DISABLE_LOGGING)
#define AVP_LOG_ENABLED() 0
#else
#define AVP_LOG_ENABLED() 1
#endif

namespace avp {
enum LogSeverity {
  LS_VERBOSE,
  LS_DEBUG,
  LS_INFO,
  LS_WARNING,
  LS_ERROR,
  LS_NONE,
};

enum LogErrorContext {
  ERRCTX_NONE,
  ERRCTX_ERRNO,

};

class LogMessage;
class LogSink {
 public:
  LogSink() {}
  virtual ~LogSink() {}
  virtual void OnLogMessage(const std::string& msg,
                            LogSeverity severity,
                            const char* tag);
  virtual void OnLogMessage(const std::string& message, LogSeverity severity);
  virtual void OnLogMessage(const std::string& message) = 0;

 private:
  friend class ::avp::LogMessage;
#if AVP_LOG_ENABLED()
  LogSink* next_ = nullptr;
  LogSeverity min_severity_;
#endif
};

namespace logging_impl {

class LogMetadata {
 public:
  LogMetadata(const char* file, int line, LogSeverity severity)
      : file_(file),
        line_and_sev_(static_cast<uint32_t>(line) << 3 |
                      static_cast<uint32_t>(severity)) {}
  LogMetadata() = default;

  const char* File() const { return file_; }
  int Line() const { return line_and_sev_ >> 3; }
  LogSeverity Severity() const {
    return static_cast<LogSeverity>(line_and_sev_ & 0x7);
  }

 private:
  const char* file_;

  // Line number and severity, the former in the most significant 29 bits, the
  // latter in the least significant 3 bits. (This is an optimization; since
  // both numbers are usually compile-time constants, this way we can load them
  // both with a single instruction.)
  uint32_t line_and_sev_;
};
static_assert(std::is_trivial_v<LogMetadata>, "");

struct LogMetadataErr {
  LogMetadata meta;
  LogErrorContext err_ctx;
  int err;
};

enum class LogArgType : uint8_t {
  kEnd = 0,
  kInt,
  kLong,
  kLongLong,
  kUInt,
  kULong,
  kULongLong,
  kDouble,
  kLongDouble,
  kCharP,
  kStdString,
  kVoidP,
  kLogMetadata,
  kLogMetadataErr,
};

template <LogArgType N, typename T>
struct Val {
  static constexpr LogArgType Type() { return N; }
  T GetVal() const { return val; }
  T val;
};

struct ToStringVal {
  static constexpr LogArgType Type() { return LogArgType::kStdString; }
  const std::string* GetVal() const { return &val; }
  std::string val;
};

inline Val<LogArgType::kInt, int> MakeVal(int x) {
  return {x};
}
inline Val<LogArgType::kLong, long> MakeVal(long x) {
  return {x};
}
inline Val<LogArgType::kLongLong, long long> MakeVal(long long x) {
  return {x};
}
inline Val<LogArgType::kUInt, unsigned int> MakeVal(unsigned int x) {
  return {x};
}
inline Val<LogArgType::kULong, unsigned long> MakeVal(unsigned long x) {
  return {x};
}
inline Val<LogArgType::kULongLong, unsigned long long> MakeVal(
    unsigned long long x) {
  return {x};
}

inline Val<LogArgType::kDouble, double> MakeVal(double x) {
  return {x};
}
inline Val<LogArgType::kLongDouble, long double> MakeVal(long double x) {
  return {x};
}

inline Val<LogArgType::kCharP, const char*> MakeVal(const char* x) {
  return {x};
}
inline Val<LogArgType::kStdString, const std::string*> MakeVal(
    const std::string& x) {
  return {&x};
}

inline Val<LogArgType::kVoidP, const void*> MakeVal(const void* x) {
  return {x};
}

inline Val<LogArgType::kLogMetadata, LogMetadata> MakeVal(
    const LogMetadata& x) {
  return {x};
}

inline Val<LogArgType::kLogMetadataErr, LogMetadataErr> MakeVal(
    const LogMetadataErr& x) {
  return {x};
}

// The enum class types are not implicitly convertible to arithmetic types.
template <typename T,
          std::enable_if_t<std::is_enum<T>::value &&
                           !std::is_arithmetic<T>::value>* = nullptr>
inline decltype(MakeVal(std::declval<std::underlying_type_t<T>>())) MakeVal(
    T x) {
  return {static_cast<std::underlying_type_t<T>>(x)};
}

template <typename T, class = std::void_t<>>
struct has_to_log_string : std::false_type {};
template <typename T>
struct has_to_log_string<
    T,
    std::void_t<decltype(ToStringValue(std::declval<T>()))>> : std::true_type {
};

template <
    typename T,
    typename T1 = std::decay_t<T>,
    typename = std::enable_if_t<
        std::is_class<T1>::value && !std::is_same<T1, std::string>::value &&
        !std::is_same<T1, LogMetadata>::value && !has_to_log_string<T1>::value>>
ToStringVal MakeVal(const T& x) {
  std::ostringstream os;
  os << x;
  return {os.str()};
}

template <typename T, typename = std::enable_if_t<has_to_log_string<T>::value>>
ToStringVal MakeVal(const T& x) {
  return {ToLogString(x)};
}

void Log(const LogArgType* fmt, ...);

template <typename... Ts>
class LogStreamer;

template <>
class LogStreamer<> final {
 public:
  template <typename U,
            typename V = decltype(MakeVal(std::declval<U>())),
            std::enable_if_t<std::is_arithmetic<U>::value ||
                             std::is_enum<U>::value>* = nullptr>
  inline LogStreamer<V> operator<<(U arg) const {
    return LogStreamer<V>(MakeVal(arg), this);
  }

  template <typename U,
            typename V = decltype(MakeVal(std::declval<U>())),
            std::enable_if_t<!std::is_arithmetic<U>::value &&
                             !std::is_enum<U>::value>* = nullptr>
  inline LogStreamer<V> operator<<(const U& arg) const {
    return LogStreamer<V>(MakeVal(arg), this);
  }

  template <typename... Us>
  inline static void Call(const Us&... args) {
    static constexpr LogArgType t[] = {Us::Type()..., LogArgType::kEnd};
    Log(t, args.GetVal()...);
  }
};

template <typename T, typename... Ts>
class LogStreamer<T, Ts...> final {
 public:
  inline LogStreamer(T arg, const LogStreamer<Ts...>* prior)
      : arg_(arg), prior_(prior) {}

  template <typename U,
            typename V = decltype(MakeVal(std::declval<U>())),
            std::enable_if_t<std::is_arithmetic<U>::value ||
                             std::is_enum<U>::value>* = nullptr>
  inline LogStreamer<V, T, Ts...> operator<<(U arg) const {
    return LogStreamer<V, T, Ts...>(MakeVal(arg), this);
  }

  template <typename U,
            typename V = decltype(MakeVal(std::declval<U>())),
            std::enable_if_t<!std::is_arithmetic<U>::value &&
                             !std::is_enum<U>::value>* = nullptr>
  inline LogStreamer<V, T, Ts...> operator<<(const U& arg) const {
    return LogStreamer<V, T, Ts...>(MakeVal(arg), this);
  }

  template <typename... Us>
  inline void Call(const Us&... args) const {
    prior_->Call(arg_, args...);
  }

 private:
  T arg_;

  const LogStreamer<Ts...>* prior_;
};

class Logger final {
 public:
  template <typename... Ts>
  inline bool operator&(const LogStreamer<Ts...>& streamer) {
    streamer.Call();
    return true;
  }
};

} /* namespace logging_impl */

class LogMessage {
 public:
  template <LogSeverity S>
  inline LogMessage(const char* file,
                    int line,
                    std::integral_constant<LogSeverity, S>)
      : LogMessage(file, line, S) {}

#if AVP_LOG_ENABLED()
  LogMessage(const char* file, int line, LogSeverity sev);
  LogMessage(const char* file,
             int line,
             LogSeverity sev,
             LogErrorContext err_ctx,
             int err);

  ~LogMessage();

  void AddTag(const char* tag);
  std::stringstream& stream();
  static int64_t LogStartTime();
  static uint32_t WallClockStartTime();
  static void LogThreads(bool on = true);
  static void LogTimestamps(bool on = true);
  static void LogToDebug(LogSeverity min_sev);
  static LogSeverity GetLogToDebug();
  static void SetLogToStderr(bool log_to_stderr);
  static void AddLogToStream(LogSink* stream, LogSeverity min_sev);
  static void RemoveLogToStream(LogSink* stream);
  static int GetLogToStream(LogSink* stream = nullptr);
  static int GetMinLogSeverity();
  static bool IsNoop(LogSeverity severity);
  template <LogSeverity S>
  inline static bool IsNoop() {
    return IsNoop(S);
  }
#else
  LogMessage(const char* file, int line, LogSeverity sev) {}
  LogMessage(const char* file,
             int line,
             LogSeverity sev,
             LogErrorContext err_ctx,
             int err) {}
  ~LogMessage() = default;

  inline void AddTag(const char* tag) {}
  inline std::stringstream& stream() { return print_stream_; }
  inline static int64_t LogStartTime() { return 0; }
  inline static uint32_t WallClockStartTime() { return 0; }
  inline static void LogThreads(bool on = true) {}
  inline static void LogTimestamps(bool on = true) {}
  inline static void LogToDebug(LogSeverity min_sev) {}
  inline static LogSeverity GetLogToDebug() { return LS_INFO; }
  inline static void SetLogToStderr(bool log_to_stderr) {}
  inline static void AddLogToStream(LogSink* stream, LogSeverity min_sev) {}
  inline static void RemoveLogToStream(LogSink* stream) {}
  inline static int GetLogToStream(LogSink* stream = nullptr) { return 0; }
  inline static int GetMinLogSeverity() { return 0; }
  static constexpr bool IsNoop(LogSeverity severity) { return true; }
  template <LogSeverity S>
  static constexpr bool IsNoop() {
    return IsNoop(S);
  }
#endif

 private:
#if AVP_LOG_ENABLED()
  static void UpdateMinLogSeverity();

  static void OutputToDebug(const std::string& msg, LogSeverity severity);

  void FinishPrintStream();

  LogSeverity severity_;

  std::string extra_;

  static LogSink* streams_;

  static std::atomic<bool> streams_empty_;

  static bool thread_, timestamp_;

  static bool log_to_stderr_;
#else
  inline static void UpdateMinLogSeverity() {}
  inline static void OutputToDebug(const std::string& msg,
                                   LogSeverity severity) {}
  inline void FinishPrintStream() {}
#endif

  std::stringstream print_stream_;

  AVP_DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

#define LOG_FILE_LINE(sev, file, line)     \
  ::avp::logging_impl::Logger() &          \
      ::avp::logging_impl::LogStreamer<>() \
          << ::avp::logging_impl::LogMetadata(file, line, sev)

#define LOG(sev) \
  !avp::LogMessage::IsNoop<sev>() && LOG_FILE_LINE(sev, __FILE__, __LINE__)

} /* namespace avp */

#endif /* !LOGGING_H */
