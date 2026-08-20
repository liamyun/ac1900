#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Config;

struct ReportFiles {
  bool ok = false;
  std::filesystem::path lv_ratio;
  std::filesystem::path tong_ratio;
  std::filesystem::path lv3;
  std::filesystem::path tong3;
  std::string mail_body;

  std::vector<std::filesystem::path> Attachments() const {
    std::vector<std::filesystem::path> files;
    if (!lv_ratio.empty()) files.push_back(lv_ratio);
    if (!tong_ratio.empty()) files.push_back(tong_ratio);
    if (!lv3.empty()) files.push_back(lv3);
    if (!tong3.empty()) files.push_back(tong3);
    return files;
  }
};

class MarketDataEngine {
 public:
  explicit MarketDataEngine(std::shared_ptr<Config> config);

  static bool IsMarketApiUrl(const std::string& url);

  // 返回 true 表示识别并处理了旧程序关心的东财接口。
  bool HandleResponse(const std::string& url, const std::string& body);

  uint64_t CurrentTrendGeneration() const;
  bool WaitForTrendAfter(uint64_t generation, int timeout_seconds);
  // 关闭程序时唤醒等待网络响应的定时线程，避免退出卡住几十秒。
  void WakeWaiters();

  ReportFiles BuildReport();

  const std::filesystem::path& data_root() const { return data_root_; }

 private:
  bool HandleKline(const std::string& url, const std::string& body);
  bool HandleTrend(const std::string& url, const std::string& body);
  void NotifyTrend();

  using Series =
      std::map<std::string, double, std::greater<std::string>>;
  Series ReadSeries(const std::filesystem::path& directory) const;
  static bool SplitFirstTwo(const std::string& line,
                            std::string& first,
                            double& second);
  static bool ParseDateTime(const std::string& text,
                            int& y, int& mon, int& d, int& h, int& min);

  std::shared_ptr<Config> config_;
  std::filesystem::path data_root_;

  mutable std::mutex trend_mutex_;
  std::condition_variable trend_cv_;
  uint64_t trend_generation_ = 0;
};
