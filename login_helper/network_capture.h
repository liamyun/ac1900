#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/internal/cef_types.h"

class Config;

class NetworkCapture {
 public:
  explicit NetworkCapture(std::shared_ptr<Config> config);
  ~NetworkCapture();

  bool enabled() const { return enabled_.load(); }
  void SetEnabled(bool enabled) { enabled_.store(enabled); }
  size_t max_body_bytes() const { return max_body_bytes_; }

  // 判断这个请求是否值得进入资源回调链。
  // ChatGPT 专用模式默认只跟踪 API/XHR，避免给 HTML/JS/CSS/图片等所有资源
  // 都安装 CefResourceRequestHandler / CefResponseFilter。
  bool ShouldTrackRequest(CefRefPtr<CefRequest> request) const;

  // 是否需要给该响应安装 CefResponseFilter。
  bool ShouldCaptureTextResponse(const std::string& url,
                                 const std::string& mime_type,
                                 cef_resource_type_t resource_type) const;

  // 在 CEF IO 线程调用。这里只做必要的数据快照并入队；JSON 转义和磁盘写入
  // 由后台 writer 线程完成，避免卡住 CEF 网络线程。
  void CaptureRequest(CefRefPtr<CefRequest> request);
  void CaptureResponse(CefRefPtr<CefRequest> request,
                       CefRefPtr<CefResponse> response,
                       std::string body,
                       cef_urlrequest_status_t status,
                       int64_t received_content_length,
                       bool truncated);

 private:
  using HeaderPairs = std::vector<std::pair<std::string, std::string>>;

  struct PendingRecord {
    enum class Kind { kRequest, kResponse };
    Kind kind = Kind::kRequest;
    std::string time;
    uint64_t id = 0;
    std::string method;
    std::string url;
    HeaderPairs headers;
    std::string body;
    bool truncated = false;
    bool queue_truncated = false;

    int response_status = 0;
    int url_request_status = 0;
    int64_t received_length = 0;
    std::string mime;
  };

  bool MatchesUrlFilter(const std::string& url) const;
  HeaderPairs SnapshotHeaders(const CefRequest::HeaderMap& headers) const;
  std::string HeadersToJson(const HeaderPairs& headers) const;
  std::string RequestBody(CefRefPtr<CefRequest> request, bool& truncated) const;
  bool IsSensitiveHeader(std::string_view name) const;
  size_t EstimateRecordBytes(const PendingRecord& record) const;

  void QueueRecord(PendingRecord record);
  void WriterMain();
  void WriteRecord(PendingRecord record);
  void AppendLine(const std::string& line);

  std::shared_ptr<Config> config_;
  std::atomic<bool> enabled_{false};
  bool include_sensitive_headers_ = false;
  bool capture_all_text_ = true;
  bool api_only_ = true;
  size_t max_body_bytes_ = 4 * 1024 * 1024;
  size_t max_queue_bytes_ = 32 * 1024 * 1024;
  std::filesystem::path capture_dir_;

  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<PendingRecord> queue_;
  size_t queued_bytes_ = 0;
  bool writer_stopping_ = false;
  std::thread writer_thread_;

  std::mutex file_mutex_;
};
