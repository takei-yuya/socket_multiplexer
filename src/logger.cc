#include "logger.h"

#include <cstring>
#include <sstream>
#include <iomanip>

void Logger::Init(std::ostream& out) {
  std::lock_guard<std::mutex> lock(instance_mutex);
  logger_ = new Logger(out);
}

Logger& Logger::GetInstance() {
  if (!logger_) {
    std::lock_guard<std::mutex> lock(instance_mutex);
    if (!logger_) {
      logger_ = new Logger(std::cout);
      *logger_ << "FATAL: Logger is not initialized yet! Use standard output to log as fallback.";
      logger_->CloseLine();
    }
  }
  return *logger_;
}

Logger *Logger::logger_ = NULL;
std::mutex Logger::instance_mutex;

Logger::LineCloserPtr Logger::OpenLine(const std::string& level, const std::string& file, int line) {
  // TODO(takei): check log level
  std::string l = level.substr(0, 1);

  time_t t;
  time(&t);
  struct tm tm;
  localtime_r(&t, &tm);

  size_t pos = file.rfind("/");
  std::string file_name = (pos != std::string::npos) ? file.substr(pos+1) : file;

  std::ostringstream prefix;
  // LMMDD HH:mm:dd file:line] msg
  // L: error level (single char)
  prefix
    << l
    << std::setw(2) << std::setfill('0') << (tm.tm_mon + 1)
    << std::setw(2) << std::setfill('0') << tm.tm_mday
    << " "
    << std::setw(2) << std::setfill('0') << tm.tm_hour
    << ":"
    << std::setw(2) << std::setfill('0') << tm.tm_min
    << ":"
    << std::setw(2) << std::setfill('0') << tm.tm_sec
    << " "
    << file_name << ":" << line << "] ";

  out_ << prefix.str();
  return Logger::LineCloserPtr(new LineCloser(*this));
}

Logger::LineCloserPtr Logger::OpenLine(const std::string& level, const std::string& file, int line,
                                       const std::string& fname, int eno) {
  // FIXME: strerror is not thread-safe (if eno is not well-known errno, strerror may construct error message with shared static memory)
  return OpenLine(level, file, line)
    <<  fname << ": "
    << std::strerror(eno) << "(" << eno << ")";
}

Logger& Logger::CloseLine() {
  out_ << std::endl;
  return *this;
}
