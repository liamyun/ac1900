#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Config;
struct ReportFiles;

class MailService {
 public:
  explicit MailService(std::shared_ptr<Config> config);

  bool enabled() const { return enabled_; }

  // 检查 POP3S 最新邮件。若出现“新邮件 + 发件人在白名单”则返回发件地址。
  // 不再像旧代码那样删除服务器邮件，只记录 Message-ID，安全一些。
  bool PollTrigger(std::string& sender);

  bool SendReport(const std::string& recipient,
                  const ReportFiles& report);

 private:
  bool CurlAvailable() const;
  bool RunCurlConfig(const std::string& config_text,
                     std::string& output,
                     DWORD timeout_ms = 60000) const;
  std::string CurlConfigQuote(const std::string& value) const;

  bool FetchLatestRawMessage(std::string& raw_message);
  std::string HeaderValue(const std::string& raw_message,
                          const std::string& header) const;
  std::string ExtractAddress(const std::string& from_header) const;
  bool SenderAllowed(const std::string& sender) const;
  std::string WrapBase64(const std::string& base64) const;
  std::string BuildMime(const std::string& recipient,
                        const ReportFiles& report) const;

  std::filesystem::path MakeTempFile(const wchar_t* prefix,
                                     const wchar_t* extension) const;

  std::shared_ptr<Config> config_;
  bool enabled_ = false;
  std::string user_;
  std::string password_;
  std::string pop_host_;
  int pop_port_ = 995;
  std::string smtp_host_;
  int smtp_port_ = 465;
  std::string allowed_senders_;
  std::filesystem::path state_file_;
};
