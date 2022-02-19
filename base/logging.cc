/*
 * logging.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "base/logging.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace avp {

namespace {
#if !defined(NDEBUG)
static LogSeverity g_min_sev = LS_INFO;
static LogSeverity g_dbg_sev = LS_INFO;
#else
static LogSeverity g_min_sev = LS_NONE;
static LogSeverity g_dbg_sev = LS_NONE;
#endif

const char* FilenameFromPath(const char* file) {
  const char* end1 = ::strrchr(file, '/');
  const char* end2 = ::strrchr(file, '\\');
  if (!end1 && !end2)
    return file;
  else
    return (end1 > end2) ? end1 + 1 : end2 + 1;
}

std::mutex g_log_mutex_;

}  // namespace

// static member
bool LogMessage::log_to_stderr_ = true;

LogSink* LogMessage::streams_ = nullptr;
std::atomic<bool> LogMessage::streams_empty_ = {true};

bool LogMessage::thread_ = true, LogMessage::timestamp_ = true;

uint64_t timestampMs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

LogMessage::LogMessage(const char* file, int line, LogSeverity sev)
    : LogMessage(file, line, sev, ERRCTX_NONE, 0) {}

LogMessage::LogMessage(const char* file,
                       int line,
                       LogSeverity sev,
                       LogErrorContext err_ctx,
                       int err)
    : severity_(sev) {
  if (timestamp_) {
    uint64_t timestamp = timestampMs();
    std::time_t time_t = time(0);

    auto localtime = std::localtime(&time_t);

    char buffer[32];

    strftime(buffer, 32, "%Y-%m-%d %T.", localtime);

    char microseconds[7];
    sprintf(microseconds, "%06llu",
            static_cast<unsigned long long>(timestamp % 1000000));

    print_stream_ << '[' << buffer << microseconds << ']';
  }

  if (thread_) {
    print_stream_ << "[" << std::this_thread::get_id() << "] ";
  }

  if (file != nullptr) {
    print_stream_ << "(" << FilenameFromPath(file) << ":" << line << "): ";
  }

  if (err_ctx != ERRCTX_NONE) {
    std::stringstream tmp;
    switch (err_ctx) {
      case ERRCTX_ERRNO:
        tmp << " " << strerror(err);
        break;
      default:
        break;
    }
    extra_ = tmp.str();
  }
}

LogMessage::~LogMessage() {
  FinishPrintStream();
  const std::string str = print_stream_.str();
  if (severity_ >= g_dbg_sev) {
    OutputToDebug(str, severity_);
  }

  std::lock_guard<std::mutex> guard(g_log_mutex_);
  for (LogSink* entry = streams_; entry != nullptr; entry = entry->next_) {
    if (severity_ >= entry->min_severity_) {
      entry->OnLogMessage(str, severity_);
    }
  }
}

void LogMessage::AddTag(const char* tag) {}

std::stringstream& LogMessage::stream() {
  return print_stream_;
}

int LogMessage::GetMinLogSeverity() {
  return static_cast<int>(g_min_sev);
}

LogSeverity LogMessage::GetLogToDebug() {
  return g_dbg_sev;
}
int64_t LogMessage::LogStartTime() {
  //  static const int64_t g_start = SystemTimeMillis();
  //  return g_start;
  return 0;
}

uint32_t LogMessage::WallClockStartTime() {
  static const uint32_t g_start_wallclock = time(nullptr);
  return g_start_wallclock;
}

void LogMessage::LogThreads(bool on) {
  thread_ = on;
}

void LogMessage::LogTimestamps(bool on) {
  timestamp_ = on;
}

void LogMessage::LogToDebug(LogSeverity min_sev) {
  g_dbg_sev = min_sev;
  std::lock_guard<std::mutex> guard(g_log_mutex_);
  UpdateMinLogSeverity();
}

void LogMessage::SetLogToStderr(bool log_to_stderr) {
  log_to_stderr_ = log_to_stderr;
}

int LogMessage::GetLogToStream(LogSink* stream) {
  std::lock_guard<std::mutex> guard(g_log_mutex_);
  LogSeverity sev = LS_NONE;
  for (LogSink* entry = streams_; entry != nullptr; entry = entry->next_) {
    if (stream == nullptr || stream == entry) {
      sev = std::min(sev, entry->min_severity_);
    }
  }
  return static_cast<int>(sev);
}

void LogMessage::AddLogToStream(LogSink* stream, LogSeverity min_sev) {
  std::lock_guard<std::mutex> guard(g_log_mutex_);
  stream->min_severity_ = min_sev;
  stream->next_ = streams_;
  streams_ = stream;
  streams_empty_.store(false, std::memory_order_relaxed);
  UpdateMinLogSeverity();
}

void LogMessage::RemoveLogToStream(LogSink* stream) {
  std::lock_guard<std::mutex> guard(g_log_mutex_);
  for (LogSink** entry = &streams_; *entry != nullptr;
       entry = &(*entry)->next_) {
    if (*entry == stream) {
      *entry = (*entry)->next_;
      break;
    }
  }
  streams_empty_.store(streams_ == nullptr, std::memory_order_relaxed);
  UpdateMinLogSeverity();
}

void LogMessage::UpdateMinLogSeverity() {
  LogSeverity min_sev = g_dbg_sev;
  for (LogSink* entry = streams_; entry != nullptr; entry = entry->next_) {
    min_sev = std::min(min_sev, entry->min_severity_);
  }
  g_min_sev = min_sev;
}

void LogMessage::OutputToDebug(const std::string& str, LogSeverity severity) {
  bool log_to_stderr = log_to_stderr_;
  if (log_to_stderr) {
    fprintf(stderr, "%s", str.c_str());
    fflush(stderr);
  }
}

// static
bool LogMessage::IsNoop(LogSeverity severity) {
  if (severity >= g_dbg_sev || severity >= g_min_sev)
    return false;
  return streams_empty_.load(std::memory_order_relaxed);
}

void LogMessage::FinishPrintStream() {
  if (!extra_.empty())
    print_stream_ << " : " << extra_;
  print_stream_ << "\n";
}

namespace logging_impl {

void Log(const LogArgType* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  LogMetadataErr meta;
  switch (*fmt) {
    case LogArgType::kLogMetadata: {
      meta = {va_arg(args, LogMetadata), ERRCTX_NONE, 0};
      break;
    }
    case LogArgType::kLogMetadataErr: {
      meta = va_arg(args, LogMetadataErr);
      break;
    }
    default: {
      va_end(args);
      return;
    }
  }

  LogMessage log_message(meta.meta.File(), meta.meta.Line(),
                         meta.meta.Severity(), meta.err_ctx, meta.err);

  for (++fmt; *fmt != LogArgType::kEnd; ++fmt) {
    switch (*fmt) {
      case LogArgType::kInt:
        log_message.stream() << va_arg(args, int);
        break;
      case LogArgType::kLong:
        log_message.stream() << va_arg(args, long);
        break;
      case LogArgType::kLongLong:
        log_message.stream() << va_arg(args, long long);
        break;
      case LogArgType::kUInt:
        log_message.stream() << va_arg(args, unsigned);
        break;
      case LogArgType::kULong:
        log_message.stream() << va_arg(args, unsigned long);
        break;
      case LogArgType::kULongLong:
        log_message.stream() << va_arg(args, unsigned long long);
        break;
      case LogArgType::kDouble:
        log_message.stream() << va_arg(args, double);
        break;
      case LogArgType::kLongDouble:
        log_message.stream() << va_arg(args, long double);
        break;
      case LogArgType::kCharP: {
        const char* s = va_arg(args, const char*);
        log_message.stream() << (s ? s : "(null)");
        break;
      }
      case LogArgType::kStdString:
        log_message.stream() << *va_arg(args, const std::string*);
        break;
      case LogArgType::kVoidP:
        log_message.stream()
            << std::hex
            << reinterpret_cast<uintptr_t>(va_arg(args, const void*));
        break;
      default:
        va_end(args);
        return;
    }
  }
  va_end(args);
}
}  // namespace logging_impl
}  // namespace avp

namespace avp {
void LogSink::OnLogMessage(const std::string& msg,
                           LogSeverity severity,
                           const char* tag) {
  OnLogMessage(tag + (": " + msg), severity);
}

void LogSink::OnLogMessage(const std::string& msg, LogSeverity /* severity */) {
  OnLogMessage(msg);
}
}  // namespace avp
