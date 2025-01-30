#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <iostream>
#include <sstream>
#include <map>
#include <ctime>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

enum class LogLevel
{
  Debug,
  Info,
  Warn,
  Error
};

struct Attribute
{
  std::string key;
  std::string value;
};

struct LogMessage
{
  std::chrono::system_clock::time_point timestamp;
  std::string body;
  LogLevel level;
  std::map<std::string, std::string> attributes;
};

inline std::string logLevelToString(LogLevel level)
{
  switch (level)
  {
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warn:
    return "WARNING";
  case LogLevel::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}

inline std::string timePointToString(const std::chrono::system_clock::time_point &tp)
{
  std::time_t tt = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
  return std::string(buffer);
}

class Logger
{
public:
  Logger(const std::string &name,
         const std::string &endpoint,
         const std::string &token,
         bool passthrough = false,
         bool insecure = false,
         bool noop = false,
         size_t maxBatchSize = 100,
         std::chrono::milliseconds batchInterval = std::chrono::milliseconds(100));

  ~Logger();

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  void debug(const std::string &message, const std::vector<Attribute> &attrs = {});
  void info(const std::string &message, const std::vector<Attribute> &attrs = {});
  void warn(const std::string &message, const std::vector<Attribute> &attrs = {});
  void error(const std::string &message, const std::exception *err = nullptr, const std::vector<Attribute> &attrs = {});

  void shutdown();

private:
  std::string serviceName_;
  std::string endpoint_;
  std::string token_;
  bool passthrough_;
  bool noop_;
  size_t maxBatchSize_;
  std::chrono::milliseconds batchInterval_;
  std::atomic<bool> stopWorker_;
  std::thread workerThread_;
  std::mutex queueMutex_;
  std::condition_variable condition_;
  std::queue<LogMessage> logQueue_;

  static std::string formatEndpoint(const std::string &endpoint, bool insecure);
  void logMessage(LogLevel level,
                  const std::string &message,
                  const std::exception *err,
                  const std::vector<Attribute> &attrs);
  void logPassthrough(LogLevel level,
                      const std::string &message,
                      const std::vector<Attribute> &attrs);
  void runBatcher();
  void sendBatch(std::vector<LogMessage> &batch);
  std::string timePointToString(const std::chrono::system_clock::time_point &tp);
};

class LoggerBuilder
{
public:
  LoggerBuilder();

  ~LoggerBuilder();

  LoggerBuilder(const LoggerBuilder &) = delete;
  LoggerBuilder &operator=(const LoggerBuilder &) = delete;

  LoggerBuilder &withName(const std::string &name);
  LoggerBuilder &withEndpoint(const std::string &endpoint);
  LoggerBuilder &withToken(const std::string &token);
  LoggerBuilder &withPassthrough(bool passthrough = true);
  LoggerBuilder &withInsecure(bool insecure = true);
  LoggerBuilder &withNoop(bool noop = true);
  LoggerBuilder &withMaxBatchSize(size_t maxBatchSize);
  LoggerBuilder &withBatchInterval(std::chrono::milliseconds batchInterval);

  Logger build();

private:
  std::string serviceName_;
  std::string endpoint_;
  std::string token_;
  bool passthrough_;
  bool insecure_;
  bool noop_;
  size_t maxBatchSize_;
  std::chrono::milliseconds batchInterval_;
};

#endif // LOGGER_H
