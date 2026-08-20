#include "market_data.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <utility>
#include "include/cef_parser.h"
#include "include/cef_values.h"

#include "config.h"
#include "util.h"

namespace {

constexpr const char* kKlineApi =
    "push2his.eastmoney.com/api/qt/stock/kline/get?secid=";
constexpr const char* kTrendApi =
    "push2.eastmoney.com/api/qt/stock/trends2/get?secid=";

std::filesystem::path Utf8Path(const std::string& s) {
  return std::filesystem::path(util::Utf8ToWide(s));
}

bool WriteListToCsv(const std::filesystem::path& path,
                    CefRefPtr<CefListValue> list) {
  if (!list) return false;
  std::ostringstream out;
  const size_t count = list->GetSize();
  for (size_t i = 0; i < count; ++i) {
    out << list->GetString(i).ToString() << "\n";
  }
  return util::WriteTextFile(path, out.str());
}

CefRefPtr<CefDictionaryValue> GetDataDictionary(
    CefRefPtr<CefValue> root) {
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    return nullptr;
  }
  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  if (!dict || !dict->HasKey("data") ||
      dict->GetType("data") != VTYPE_DICTIONARY) {
    return nullptr;
  }
  return dict->GetDictionary("data");
}

}  // namespace

MarketDataEngine::MarketDataEngine(std::shared_ptr<Config> config)
    : config_(std::move(config)) {
  data_root_ = config_->GetPath(
      L"market", L"data_root", config_->path().parent_path());
  util::EnsureDir(data_root_);
}

bool MarketDataEngine::IsMarketApiUrl(const std::string& url) {
  return url.find(kKlineApi) != std::string::npos ||
         url.find(kTrendApi) != std::string::npos;
}

bool MarketDataEngine::HandleResponse(const std::string& url,
                                      const std::string& body) {
  if (url.find(kKlineApi) != std::string::npos) {
    return HandleKline(url, body);
  }
  if (url.find(kTrendApi) != std::string::npos) {
    const bool result = HandleTrend(url, body);
    // 老程序无论解析成功与否都会 SetEvent；这里保持同样行为，
    // 避免定时任务因为接口临时格式问题永久卡住。
    NotifyTrend();
    return result;
  }
  return false;
}

bool MarketDataEngine::HandleKline(const std::string& url,
                                   const std::string& body) {
  CefRefPtr<CefValue> value =
      CefParseJSON(CefString(body), JSON_PARSER_ALLOW_TRAILING_COMMAS);
  CefRefPtr<CefDictionaryValue> data = GetDataDictionary(value);
  if (!data || !data->HasKey("name") ||
      !data->HasKey("klines") ||
      data->GetType("klines") != VTYPE_LIST) {
    return false;
  }

  const std::string name = data->GetString("name").ToString();
  CefRefPtr<CefListValue> list = data->GetList("klines");
  if (name.empty() || !list || list->GetSize() == 0) {
    return false;
  }

  std::wstring filename = L"kline.csv";
  if (url.find("klt=101") != std::string::npos) {
    filename = L"日线.csv";
  } else if (url.find("klt=102") != std::string::npos) {
    filename = L"周线.csv";
  } else if (url.find("klt=103") != std::string::npos) {
    filename = L"月线.csv";
  }

  const auto dir = data_root_ / Utf8Path(name);
  util::EnsureDir(dir);
  return WriteListToCsv(dir / filename, list);
}

bool MarketDataEngine::HandleTrend(const std::string& url,
                                   const std::string& body) {
  std::string json = body;
  // 东财经常返回 JSONP：callback({...})
  const size_t begin = json.find('(');
  const size_t end = json.rfind(')');
  if (begin != std::string::npos && end != std::string::npos &&
      end > begin) {
    json = json.substr(begin + 1, end - begin - 1);
  }

  CefRefPtr<CefValue> value =
      CefParseJSON(CefString(json), JSON_PARSER_ALLOW_TRAILING_COMMAS);
  CefRefPtr<CefDictionaryValue> data = GetDataDictionary(value);
  if (!data || !data->HasKey("code") ||
      !data->HasKey("trends") ||
      data->GetType("trends") != VTYPE_LIST) {
    return false;
  }

  const std::string code = data->GetString("code").ToString();
  CefRefPtr<CefListValue> list = data->GetList("trends");
  if (code.empty() || !list || list->GetSize() == 0) {
    return false;
  }

  const std::string latest =
      list->GetString(list->GetSize() - 1).ToString();
  std::string timestamp;
  double ignored = 0.0;
  if (!SplitFirstTwo(latest, timestamp, ignored)) {
    return false;
  }

  int y = 0, mon = 0, d = 0, h = 0, min = 0;
  if (!ParseDateTime(timestamp, y, mon, d, h, min)) {
    return false;
  }

  // 完全保留旧逻辑：USDCNH 只在最新数据时间 <= 04:59 时落盘。
  if (_stricmp(code.c_str(), "USDCNH") == 0 && h > 4) {
    return true;
  }

  char date_name[32]{};
  std::snprintf(date_name, sizeof(date_name), "%d-%d-%d.csv", y, mon, d);

  const auto dir = data_root_ / Utf8Path(code);
  util::EnsureDir(dir);
  return WriteListToCsv(dir / util::Utf8ToWide(date_name), list);
}

void MarketDataEngine::NotifyTrend() {
  {
    std::lock_guard lock(trend_mutex_);
    ++trend_generation_;
  }
  trend_cv_.notify_all();
}

uint64_t MarketDataEngine::CurrentTrendGeneration() const {
  std::lock_guard lock(trend_mutex_);
  return trend_generation_;
}

bool MarketDataEngine::WaitForTrendAfter(uint64_t generation,
                                         int timeout_seconds) {
  std::unique_lock lock(trend_mutex_);
  return trend_cv_.wait_for(
      lock, std::chrono::seconds(timeout_seconds),
      [&] { return trend_generation_ > generation; });
}

void MarketDataEngine::WakeWaiters() {
  NotifyTrend();
}

bool MarketDataEngine::SplitFirstTwo(const std::string& line,
                                     std::string& first,
                                     double& second) {
  const size_t comma = line.find(',');
  if (comma == std::string::npos) {
    return false;
  }
  first = util::Trim(line.substr(0, comma));
  const size_t second_comma = line.find(',', comma + 1);
  const std::string value =
      util::Trim(line.substr(comma + 1, second_comma - comma - 1));
  if (first.empty() || value.empty()) {
    return false;
  }
  char* end = nullptr;
  second = std::strtod(value.c_str(), &end);
  return end && end != value.c_str();
}

bool MarketDataEngine::ParseDateTime(const std::string& text,
                                     int& y, int& mon, int& d,
                                     int& h, int& min) {
  // %d 前的空白会吃掉任意数量空格，因此兼容旧 CSV 里的三个空格。
  return std::sscanf(text.c_str(), "%d-%d-%d %d:%d",
                     &y, &mon, &d, &h, &min) == 5;
}

MarketDataEngine::Series MarketDataEngine::ReadSeries(
    const std::filesystem::path& directory) const {
  Series result;
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec) || ec) {
    return result;
  }

  for (const auto& entry :
       std::filesystem::directory_iterator(directory, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec) || ec) continue;
    if (_wcsicmp(entry.path().extension().c_str(), L".csv") != 0) continue;

    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) continue;

    std::string line;
    while (std::getline(in, line)) {
      line = util::Trim(std::move(line));
      if (line.empty()) continue;
      std::string key;
      double value = 0.0;
      if (SplitFirstTwo(line, key, value)) {
        result[key] = value;
      }
    }
  }
  return result;
}

ReportFiles MarketDataEngine::BuildReport() {
  const Series alm = ReadSeries(data_root_ / L"alm");
  const Series lalt = ReadSeries(data_root_ / L"LALT");
  const Series usdcnh = ReadSeries(data_root_ / L"USDCNH");
  const Series cum = ReadSeries(data_root_ / L"cum");
  const Series lcpt = ReadSeries(data_root_ / L"LCPT");

  ReportFiles report;
  report.lv_ratio = data_root_ / L"lv-ratio.csv";
  report.tong_ratio = data_root_ / L"tong-ratio.csv";
  report.lv3 = data_root_ / L"lv3.csv";
  report.tong3 = data_root_ / L"tong3.csv";

  std::ofstream f_lv(report.lv_ratio, std::ios::binary | std::ios::trunc);
  std::ofstream f_tong(report.tong_ratio, std::ios::binary | std::ios::trunc);
  if (!f_lv || !f_tong) {
    return report;
  }

  std::vector<std::string> lv3_lines;
  std::vector<std::string> tong3_lines;
  constexpr double kEpsilon = 0.0001;

  auto lookup = [](const Series& series,
                   const std::string& key) -> double {
    const auto it = series.find(key);
    return it == series.end() ? 0.0 : it->second;
  };

  for (const auto& [time, usd] : usdcnh) {
    const double lcpt_now = lookup(lcpt, time);
    const double cum_now = lookup(cum, time);
    const double lalt_now = lookup(lalt, time);
    const double alm_now = lookup(alm, time);

    int y = 0, mon = 0, d = 0, h = 0, min = 0;
    const bool time_ok = ParseDateTime(time, y, mon, d, h, min);
    const bool is_three_point =
        time_ok && min == 0 && (h == 10 || h == 14 || h == 22);

    if (std::abs(lcpt_now) >= kEpsilon &&
        std::abs(cum_now) >= kEpsilon) {
      const double ratio = (lcpt_now * usd * 1.13) / cum_now - 1.0;
      std::ostringstream line;
      line << time << "," << std::fixed << std::setprecision(4)
           << usd << "," << lcpt_now << "," << cum_now << ","
           << ratio << "\n";
      f_tong << line.str();
      if (is_three_point) tong3_lines.push_back(line.str());
    }

    if (std::abs(lalt_now) >= kEpsilon &&
        std::abs(alm_now) >= kEpsilon) {
      const double ratio = (lalt_now * usd * 1.13) / alm_now - 1.0;
      std::ostringstream line;
      line << time << "," << std::fixed << std::setprecision(4)
           << usd << "," << lalt_now << "," << alm_now << ","
           << ratio << "\n";
      f_lv << line.str();
      if (is_three_point) lv3_lines.push_back(line.str());
    }
  }

  f_lv.close();
  f_tong.close();

  std::ostringstream lv3_text;
  for (const auto& line : lv3_lines) lv3_text << line;
  std::ostringstream tong3_text;
  for (const auto& line : tong3_lines) tong3_text << line;

  if (!util::WriteTextFile(report.lv3, lv3_text.str()) ||
      !util::WriteTextFile(report.tong3, tong3_text.str())) {
    return report;
  }

  std::ostringstream body;
  body << "铝（三个时间点）\r\n"
       << lv3_text.str()
       << "\r\n铜（三个时间点）\r\n"
       << tong3_text.str()
       << "\r\n附件说明：\r\n"
       << "lv-ratio.csv --- 铝全部分时\r\n"
       << "tong-ratio.csv --- 铜全部分时\r\n"
       << "lv3.csv --- 铝每天 10/14/22 点\r\n"
       << "tong3.csv --- 铜每天 10/14/22 点\r\n";
  report.mail_body = body.str();
  report.ok = true;
  return report;
}
