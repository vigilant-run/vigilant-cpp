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
#include <iomanip>

#include "logger.h"

Logger::Logger(const std::string &name,
               const std::string &endpoint,
               const std::string &token,
               bool passthrough,
               bool insecure,
               bool noop,
               size_t maxBatchSize,
               std::chrono::milliseconds batchInterval)
    : serviceName_(name),
      endpoint_(formatEndpoint(endpoint, insecure)),
      token_(token),
      passthrough_(passthrough),
      noop_(noop),
      maxBatchSize_(maxBatchSize),
      batchInterval_(batchInterval),
      stopWorker_(false)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
  workerThread_ = std::thread(&Logger::runBatcher, this);
}

Logger::~Logger()
{
  shutdown();
}

void Logger::debug(const std::string &message, const std::vector<Attribute> &attrs)
{
  logMessage(LogLevel::Debug, message, nullptr, attrs);
}

void Logger::info(const std::string &message, const std::vector<Attribute> &attrs)
{
  logMessage(LogLevel::Info, message, nullptr, attrs);
}

void Logger::warn(const std::string &message, const std::vector<Attribute> &attrs)
{
  logMessage(LogLevel::Warn, message, nullptr, attrs);
}

void Logger::error(const std::string &message, const std::exception *err, const std::vector<Attribute> &attrs)
{
  logMessage(LogLevel::Error, message, err, attrs);
}

void Logger::shutdown()
{
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (stopWorker_)
    {
      return;
    }
    stopWorker_ = true;
  }
  condition_.notify_all();
  if (workerThread_.joinable())
  {
    workerThread_.join();
  }
  curl_global_cleanup();
}

std::string Logger::formatEndpoint(const std::string &endpoint, bool insecure)
{
  std::ostringstream oss;
  if (insecure)
  {
    oss << "http://" << endpoint << "/api/message";
  }
  else
  {
    oss << "https://" << endpoint << "/api/message";
  }
  return oss.str();
}

void Logger::logMessage(LogLevel level,
                        const std::string &message,
                        const std::exception *err,
                        const std::vector<Attribute> &attrs)
{
  if (noop_)
    return;

  LogMessage lm;
  lm.timestamp = std::chrono::system_clock::now();
  lm.body = message;
  lm.level = level;

  lm.attributes["service.name"] = serviceName_;
  for (auto &attr : attrs)
  {
    lm.attributes[attr.key] = attr.value;
  }
  if (err != nullptr)
  {
    lm.attributes["error"] = err->what();
  }

  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    logQueue_.push(std::move(lm));
  }
  condition_.notify_all();

  logPassthrough(level, message, attrs);
}

void Logger::logPassthrough(LogLevel level,
                            const std::string &message,
                            const std::vector<Attribute> &attrs)
{
  if (!passthrough_)
    return;
  std::ostringstream oss;
  oss << "[" << logLevelToString(level) << "] " << message << " {";
  for (auto &a : attrs)
  {
    oss << a.key << "=" << a.value << " ";
  }
  oss << "}";
  std::cout << oss.str() << std::endl;
}

void Logger::runBatcher()
{
  std::vector<LogMessage> batch;
  batch.reserve(maxBatchSize_);

  auto nextSendTime = std::chrono::steady_clock::now() + batchInterval_;

  while (true)
  {
    std::unique_lock<std::mutex> lock(queueMutex_);
    condition_.wait_until(lock, nextSendTime, [&]()
                          { return !logQueue_.empty() || stopWorker_; });

    if (stopWorker_ && logQueue_.empty())
    {
      if (!batch.empty())
      {
        sendBatch(batch);
      }
      break;
    }

    while (!logQueue_.empty() && batch.size() < maxBatchSize_)
    {
      batch.push_back(std::move(logQueue_.front()));
      logQueue_.pop();
    }
    lock.unlock();

    if (batch.size() >= maxBatchSize_)
    {
      sendBatch(batch);
    }

    auto now = std::chrono::steady_clock::now();
    if (now >= nextSendTime && !batch.empty())
    {
      sendBatch(batch);
    }

    nextSendTime = std::chrono::steady_clock::now() + batchInterval_;
  }
}

void Logger::sendBatch(std::vector<LogMessage> &batch)
{
  if (batch.empty())
  {
    return;
  }

  nlohmann::json jsonPayload;
  jsonPayload["token"] = token_;
  jsonPayload["type"] = "logs";

  nlohmann::json logsArray = nlohmann::json::array();
  for (auto &msg : batch)
  {
    nlohmann::json msgJson;
    msgJson["timestamp"] = timePointToString(msg.timestamp);
    msgJson["body"] = msg.body;
    msgJson["level"] = logLevelToString(msg.level);

    nlohmann::json attributesJson;
    for (auto &kv : msg.attributes)
    {
      attributesJson[kv.first] = kv.second;
    }
    msgJson["attributes"] = attributesJson;

    logsArray.push_back(msgJson);
  }

  jsonPayload["logs"] = logsArray;
  std::string payloadStr = jsonPayload.dump();

  CURL *curl = curl_easy_init();
  if (curl)
  {
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payloadStr.size());

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
      std::cerr << "Failed to send logs: " << curl_easy_strerror(res) << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }

  batch.clear();
}

std::string Logger::timePointToString(const std::chrono::system_clock::time_point &tp)
{
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::tm *gmt = std::gmtime(&tt);

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      tp.time_since_epoch() % std::chrono::seconds(1));

  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", gmt);

  std::ostringstream oss;
  oss << buffer << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
  return oss.str();
}

LoggerBuilder::LoggerBuilder()
    : serviceName_("my_server"),
      endpoint_("ingress.vigilant.run"),
      token_("tk_1234567890"),
      passthrough_(true),
      insecure_(false),
      noop_(false),
      maxBatchSize_(1000),
      batchInterval_(std::chrono::milliseconds(100))
{
}

LoggerBuilder::~LoggerBuilder()
{
}

LoggerBuilder &LoggerBuilder::withName(const std::string &name)
{
  serviceName_ = name;
  return *this;
}

LoggerBuilder &LoggerBuilder::withEndpoint(const std::string &endpoint)
{
  endpoint_ = endpoint;
  return *this;
}

LoggerBuilder &LoggerBuilder::withToken(const std::string &token)
{
  token_ = token;
  return *this;
}

LoggerBuilder &LoggerBuilder::withPassthrough(bool passthrough)
{
  passthrough_ = passthrough;
  return *this;
}

LoggerBuilder &LoggerBuilder::withInsecure(bool insecure)
{
  insecure_ = insecure;
  return *this;
}

LoggerBuilder &LoggerBuilder::withNoop(bool noop)
{
  noop_ = noop;
  return *this;
}

LoggerBuilder &LoggerBuilder::withMaxBatchSize(size_t maxBatchSize)
{
  maxBatchSize_ = maxBatchSize;
  return *this;
}

LoggerBuilder &LoggerBuilder::withBatchInterval(std::chrono::milliseconds batchInterval)
{
  batchInterval_ = batchInterval;
  return *this;
}

Logger LoggerBuilder::build()
{
  return Logger(serviceName_, endpoint_, token_, passthrough_, insecure_, noop_, maxBatchSize_, batchInterval_);
}