#ifndef SOCKET_MULTIPLEXER_LOGGER_H_
#define SOCKET_MULTIPLEXER_LOGGER_H_

#include <iostream>
#include <mutex>
#include <string>
#include <memory>

#define LOG(level) \
  Logger::GetInstance().OpenLine(#level, __FILE__, __LINE__)

class Logger {
public:
  // TODO(takei): more options for logging
  static void Init(std::ostream& out);
  static Logger& GetInstance();

  Logger(std::ostream& out) : out_(out) {}

  Logger() = delete;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  class LineCloser {
  private:
    friend class Logger;
    LineCloser(Logger& logger) : logger_(logger) {}

  public:
    ~LineCloser() { logger_.CloseLine(); }

    LineCloser() = delete;
    LineCloser(LineCloser&) = delete;
    LineCloser& operator=(const LineCloser&) = delete;

    template <typename T> LineCloser& operator<<(T x) {
      logger_ << x;
      return *this;
    }

  private:
    Logger& logger_;
  };
  typedef std::shared_ptr<LineCloser> LineCloserPtr;

  LineCloserPtr OpenLine(const std::string& level, const std::string& file, int line);
  Logger& CloseLine();

  template <typename T>
  Logger& operator<<(const T x) {
    out_ << x;
    return *this;
  }

private:
  static Logger *logger_;
  static std::mutex instance_mutex;

  std::ostream& out_;
};

template <typename T>
Logger::LineCloserPtr operator<<(Logger::LineCloserPtr lc, T x) {
  *lc << x;
  return lc;
}

#endif  // SOCKET_MULTIPLEXER_LOGGER_H_
