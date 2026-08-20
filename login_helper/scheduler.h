#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class ClientHandler;
class Config;
class MailService;
class MarketDataEngine;

class Scheduler {
 public:
  Scheduler(ClientHandler* handler,
            std::shared_ptr<Config> config,
            MarketDataEngine* market,
            MailService* mail);
  ~Scheduler();

  void Start();
  void Stop();

 private:
  struct LocalClock {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
  };

  void ThreadMain();
  void FireScheduledJobs(const LocalClock& st);
  void CheckMail();
  bool RunMarketPage(const std::string& url);
  bool ShouldFireOnceToday(const std::string& key,
                           const std::string& hhmm,
                           const LocalClock& st);
  void MarkFiredToday(const std::string& key, const LocalClock& st);
  std::string TodayKey(const LocalClock& st) const;

  ClientHandler* handler_ = nullptr;  // 由 ClientHandler 拥有，Stop 后不再访问。
  std::shared_ptr<Config> config_;
  MarketDataEngine* market_ = nullptr;
  MailService* mail_ = nullptr;

  std::atomic<bool> running_{false};
  std::thread worker_;
  std::mutex stop_mutex_;
  std::condition_variable stop_cv_;

  std::unordered_map<std::string, std::string> fired_date_;
  int loop_counter_ = 0;
};
