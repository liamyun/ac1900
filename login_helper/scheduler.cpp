#include "scheduler.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include <utility>
#include "client_handler.h"
#include "config.h"
#include "mail_service.h"
#include "market_data.h"
#include "util.h"

Scheduler::Scheduler(ClientHandler* handler,
                     std::shared_ptr<Config> config,
                     MarketDataEngine* market,
                     MailService* mail)
    : handler_(handler),
      config_(std::move(config)),
      market_(market),
      mail_(mail) {}

Scheduler::~Scheduler() {
  Stop();
}

void Scheduler::Start() {
  if (running_.exchange(true)) {
    return;
  }
  worker_ = std::thread(&Scheduler::ThreadMain, this);
}

void Scheduler::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  stop_cv_.notify_all();
  if (market_) {
    market_->WakeWaiters();
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::string Scheduler::TodayKey(const LocalClock& st) const {
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                st.year, st.month, st.day);
  return buf;
}

bool Scheduler::ShouldFireOnceToday(const std::string& key,
                                    const std::string& hhmm,
                                    const LocalClock& st) {
  int hour = 0;
  int minute = 0;
  if (!util::ParseHHMM(hhmm, hour, minute) ||
      hour != st.hour || minute != st.minute) {
    return false;
  }
  const std::string today = TodayKey(st);
  auto it = fired_date_.find(key);
  if (it != fired_date_.end() && it->second == today) {
    return false;
  }
  return true;
}

void Scheduler::MarkFiredToday(const std::string& key, const LocalClock& st) {
  fired_date_[key] = TodayKey(st);
}

bool Scheduler::RunMarketPage(const std::string& url) {
  if (!handler_ || !market_) {
    return false;
  }

  const uint64_t generation = market_->CurrentTrendGeneration();
  handler_->Navigate(url);
  const int timeout =
      std::max(5, config_->GetInt(L"schedule", L"response_timeout_seconds", 30));
  const bool got = market_->WaitForTrendAfter(generation, timeout);
  return running_.load() && got;
}

void Scheduler::CheckMail() {
  if (!mail_ || !mail_->enabled() || !market_) {
    return;
  }

  std::string sender;
  if (mail_->PollTrigger(sender)) {
    const ReportFiles report = market_->BuildReport();
    if (report.ok) {
      mail_->SendReport(sender, report);
    }
  }
}

void Scheduler::FireScheduledJobs(const LocalClock& st) {
  if (!config_->GetBool(L"schedule", L"enabled", true)) {
    return;
  }

  const std::string lcpt_time =
      config_->GetString(L"schedule", L"lcpt_time", "03:03");
  const std::string lalt_time =
      config_->GetString(L"schedule", L"lalt_time", "03:05");
  const std::string usd_time =
      config_->GetString(L"schedule", L"usdcnh_time", "04:55");
  const std::string cum_time =
      config_->GetString(L"schedule", L"cum_time", "15:03");
  const std::string alm_time =
      config_->GetString(L"schedule", L"alm_time", "15:05");

  if (ShouldFireOnceToday("LCPT", lcpt_time, st) &&
      RunMarketPage("https://wap.eastmoney.com/quote/stock/109.LCPT.html")) {
    MarkFiredToday("LCPT", st);
  }
  if (ShouldFireOnceToday("LALT", lalt_time, st) &&
      RunMarketPage("https://wap.eastmoney.com/quote/stock/109.LALT.html")) {
    MarkFiredToday("LALT", st);
  }
  if (ShouldFireOnceToday("USDCNH", usd_time, st) &&
      RunMarketPage("https://wap.eastmoney.com/quote/stock/133.USDCNH.html")) {
    MarkFiredToday("USDCNH", st);
  }
  if (ShouldFireOnceToday("cum", cum_time, st) &&
      RunMarketPage("https://wap.eastmoney.com/quote/stock/113.cum.html")) {
    MarkFiredToday("cum", st);
  }
  if (ShouldFireOnceToday("alm", alm_time, st)) {
    const bool got_data =
        RunMarketPage("https://wap.eastmoney.com/quote/stock/113.alm.html");
    if (got_data) {
      MarkFiredToday("alm", st);
    }
    if (got_data && market_) {
      const ReportFiles report = market_->BuildReport();

      // 兼容旧 set.ini：[email] e=收件人。
      const std::string recipient =
          config_->GetString(L"email", L"e", "");
      if (report.ok && mail_ && mail_->enabled() && !recipient.empty()) {
        mail_->SendReport(recipient, report);
      }
    }
  }
}

void Scheduler::ThreadMain() {
  // 旧程序 OnLoadEnd 后等 1 秒再开始；这里同样留一点启动时间。
  {
    std::unique_lock lock(stop_mutex_);
    if (stop_cv_.wait_for(lock, std::chrono::seconds(1),
                          [&] { return !running_.load(); })) {
      return;
    }
  }

  const int poll_seconds =
      std::max(1, config_->GetInt(L"schedule", L"poll_seconds", 5));
  // 兼容旧 [email] allnum=13：每 13 个循环检查一次邮件。
  const int mail_cycles =
      std::max(1, config_->GetInt(L"email", L"allnum", 13));

  while (running_.load()) {
    SYSTEMTIME system_time{};
    ::GetLocalTime(&system_time);
    const LocalClock st{
        static_cast<int>(system_time.wYear),
        static_cast<int>(system_time.wMonth),
        static_cast<int>(system_time.wDay),
        static_cast<int>(system_time.wHour),
        static_cast<int>(system_time.wMinute)};
    FireScheduledJobs(st);

    ++loop_counter_;
    if (loop_counter_ >= mail_cycles) {
      loop_counter_ = 0;
      CheckMail();
    }

    std::unique_lock lock(stop_mutex_);
    stop_cv_.wait_for(lock, std::chrono::seconds(poll_seconds),
                      [&] { return !running_.load(); });
  }
}
