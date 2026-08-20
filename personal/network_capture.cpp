#include "network_capture.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <sstream>
#include <utility>
#include <vector>

#include "config.h"
#include "util.h"

namespace {

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool ContainsAny(const std::string& haystack,
                 std::initializer_list<const char*> needles) {
  for (const char* n : needles) {
    if (haystack.find(n) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void WriteBodyJson(std::ostringstream& out, std::string_view body) {
  if (util::IsValidUtf8(body)) {
    out << ",\"body_encoding\":\"utf-8\""
        << ",\"body\":\"" << util::JsonEscape(body) << "\"";
  } else {
    out << ",\"body_encoding\":\"base64\""
        << ",\"body_base64\":\"" << util::Base64Encode(body) << "\"";
  }
}


}  // namespace

NetworkCapture::NetworkCapture(std::shared_ptr<Config> config)
    : config_(std::move(config)) {
  enabled_.store(config_->GetBool(L"capture", L"enabled", false));
  include_sensitive_headers_ =
      config_->GetBool(L"capture", L"include_sensitive_headers", false);
  capture_all_text_ =
      config_->GetBool(L"capture", L"capture_all_text", true);
  api_only_ = config_->GetBool(L"capture", L"api_only", true);

  const int max_mb = std::max(1, config_->GetInt(L"capture", L"max_body_mb", 4));
  max_body_bytes_ = static_cast<size_t>(max_mb) * 1024 * 1024;

  const int queue_mb =
      std::max(4, config_->GetInt(L"capture", L"max_queue_mb", 32));
  max_queue_bytes_ = static_cast<size_t>(queue_mb) * 1024 * 1024;

  capture_dir_ = config_->GetPath(
      L"capture", L"directory",
      config_->path().parent_path() / L"capture");
  util::EnsureDir(capture_dir_);

  writer_thread_ = std::thread(&NetworkCapture::WriterMain, this);
}

NetworkCapture::~NetworkCapture() {
  {
    std::lock_guard lock(queue_mutex_);
    writer_stopping_ = true;
  }
  queue_cv_.notify_all();
  if (writer_thread_.joinable()) {
    writer_thread_.join();
  }
}

bool NetworkCapture::MatchesUrlFilter(const std::string& url) const {
  const std::string filters =
      LowerAscii(config_->GetString(L"capture", L"url_contains", ""));
  if (filters.empty()) {
    return false;
  }

  std::stringstream ss(filters);
  std::string item;
  const std::string lowered_url = LowerAscii(url);
  while (std::getline(ss, item, ';')) {
    item = util::Trim(item);
    if (!item.empty() && lowered_url.find(item) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool NetworkCapture::ShouldTrackRequest(CefRefPtr<CefRequest> request) const {
  if (!enabled_.load() || !request) {
    return false;
  }

  const std::string url = request->GetURL().ToString();
  if (!capture_all_text_) {
    return MatchesUrlFilter(url);
  }

  if (!api_only_) {
    return true;
  }

  // Chromium/CEF 把 XMLHttpRequest/fetch 一类 API 请求归入 RT_XHR。
  // 另外保留非 GET 请求，避免漏掉少数不是 RT_XHR 的 POST API。
  if (request->GetResourceType() == RT_XHR) {
    return true;
  }
  return LowerAscii(request->GetMethod().ToString()) != "get";
}

bool NetworkCapture::ShouldCaptureTextResponse(
    const std::string& url,
    const std::string& mime_type,
    cef_resource_type_t resource_type) const {
  if (!enabled_.load()) {
    return false;
  }
  if (!capture_all_text_) {
    if (!MatchesUrlFilter(url)) {
      return false;
    }
  } else if (api_only_ && resource_type != RT_XHR) {
    // ShouldTrackRequest 对非 GET 做了兜底，所以这里不再次仅凭 resource type
    // 拒绝；MIME 仍需是文本类。
  }

  const std::string mime = LowerAscii(mime_type);
  if (mime.empty()) {
    // XHR/fetch 某些响应拿不到 MIME；最终仍受 max_body_mb 限制。
    return true;
  }
  return mime.rfind("text/", 0) == 0 ||
         ContainsAny(mime, {"json", "javascript", "xml", "x-www-form-urlencoded",
                            "graphql", "event-stream"});
}

bool NetworkCapture::IsSensitiveHeader(std::string_view name) const {
  std::string lowered(name);
  lowered = LowerAscii(std::move(lowered));
  return lowered == "authorization" || lowered == "proxy-authorization" ||
         lowered == "cookie" || lowered == "set-cookie";
}

NetworkCapture::HeaderPairs NetworkCapture::SnapshotHeaders(
    const CefRequest::HeaderMap& headers) const {
  HeaderPairs result;
  result.reserve(headers.size());
  for (const auto& [k, v] : headers) {
    std::string key = k.ToString();
    std::string value = v.ToString();
    if (!include_sensitive_headers_ && IsSensitiveHeader(key)) {
      value = "<redacted>";
    }
    result.emplace_back(std::move(key), std::move(value));
  }
  return result;
}

std::string NetworkCapture::HeadersToJson(const HeaderPairs& headers) const {
  std::ostringstream out;
  out << "{";
  bool first = true;
  for (const auto& [key, value] : headers) {
    if (!first) out << ",";
    first = false;
    out << "\"" << util::JsonEscape(key) << "\":\""
        << util::JsonEscape(value) << "\"";
  }
  out << "}";
  return out.str();
}

std::string NetworkCapture::RequestBody(CefRefPtr<CefRequest> request,
                                        bool& truncated) const {
  truncated = false;
  CefRefPtr<CefPostData> post = request->GetPostData();
  if (!post) {
    return {};
  }

  CefPostData::ElementVector elements;
  post->GetElements(elements);
  std::string body;

  for (const auto& element : elements) {
    if (!element) continue;

    if (element->GetType() == PDE_TYPE_BYTES) {
      const size_t count = element->GetBytesCount();
      if (count == 0) continue;
      const size_t remaining =
          body.size() < max_body_bytes_ ? max_body_bytes_ - body.size() : 0;
      if (remaining == 0) {
        truncated = true;
        break;
      }
      const size_t to_read = std::min(count, remaining);
      std::string bytes(to_read, '\0');
      const size_t got = element->GetBytes(to_read, bytes.data());
      bytes.resize(got);
      body.append(bytes);
      if (got < count) {
        truncated = true;
        break;
      }
    } else if (element->GetType() == PDE_TYPE_FILE) {
      if (!body.empty()) body += "\n";
      body += "<file:";
      body += element->GetFile().ToString();
      body += ">";
    }
  }
  return body;
}

void NetworkCapture::CaptureRequest(CefRefPtr<CefRequest> request) {
  if (!enabled_.load() || !request) {
    return;
  }

  CefRequest::HeaderMap headers;
  request->GetHeaderMap(headers);

  PendingRecord record;
  record.kind = PendingRecord::Kind::kRequest;
  record.time = util::LocalTimestamp();
  record.id = request->GetIdentifier();
  record.method = request->GetMethod().ToString();
  record.url = request->GetURL().ToString();
  record.headers = SnapshotHeaders(headers);
  record.body = RequestBody(request, record.truncated);
  QueueRecord(std::move(record));
}

void NetworkCapture::CaptureResponse(CefRefPtr<CefRequest> request,
                                     CefRefPtr<CefResponse> response,
                                     std::string body,
                                     cef_urlrequest_status_t status,
                                     int64_t received_content_length,
                                     bool truncated) {
  if (!enabled_.load() || !request || !response) {
    return;
  }

  CefResponse::HeaderMap headers;
  response->GetHeaderMap(headers);

  PendingRecord record;
  record.kind = PendingRecord::Kind::kResponse;
  record.time = util::LocalTimestamp();
  record.id = request->GetIdentifier();
  record.url = request->GetURL().ToString();
  record.response_status = response->GetStatus();
  record.url_request_status = static_cast<int>(status);
  record.received_length = received_content_length;
  record.mime = response->GetMimeType().ToString();
  record.headers = SnapshotHeaders(headers);
  record.body = std::move(body);
  record.truncated = truncated;
  QueueRecord(std::move(record));
}

size_t NetworkCapture::EstimateRecordBytes(const PendingRecord& record) const {
  size_t total = record.time.size() + record.method.size() + record.url.size() +
                 record.body.size() + record.mime.size() + 256;
  for (const auto& [key, value] : record.headers) {
    total += key.size() + value.size() + 16;
  }
  return total;
}

void NetworkCapture::QueueRecord(PendingRecord record) {
  size_t estimate = EstimateRecordBytes(record);
  {
    std::lock_guard lock(queue_mutex_);
    if (writer_stopping_) {
      return;
    }

    // 后台磁盘如果暂时跟不上，绝不能让 CEF IO 线程无限堆内存。
    // 超限时仅丢掉正文，保留 URL/状态/头部元数据并标记 queue_truncated。
    if (queued_bytes_ + estimate > max_queue_bytes_ && !record.body.empty()) {
      estimate -= record.body.size();
      record.body.clear();
      record.truncated = true;
      record.queue_truncated = true;
    }

    queued_bytes_ += estimate;
    queue_.push_back(std::move(record));
  }
  queue_cv_.notify_one();
}

void NetworkCapture::WriterMain() {
  for (;;) {
    PendingRecord record;
    {
      std::unique_lock lock(queue_mutex_);
      queue_cv_.wait(lock, [&] { return writer_stopping_ || !queue_.empty(); });
      if (queue_.empty()) {
        if (writer_stopping_) {
          break;
        }
        continue;
      }
      record = std::move(queue_.front());
      queue_.pop_front();
      const size_t estimate = EstimateRecordBytes(record);
      queued_bytes_ = estimate <= queued_bytes_ ? queued_bytes_ - estimate : 0;
    }
    WriteRecord(std::move(record));
  }
}

void NetworkCapture::WriteRecord(PendingRecord record) {
  std::ostringstream line;
  line << "{\"time\":\"" << util::JsonEscape(record.time) << "\"";
  if (record.kind == PendingRecord::Kind::kRequest) {
    line << ",\"direction\":\"request\",\"id\":" << record.id
         << ",\"method\":\"" << util::JsonEscape(record.method)
         << "\",\"url\":\"" << util::JsonEscape(record.url)
         << "\",\"headers\":" << HeadersToJson(record.headers);
  } else {
    line << ",\"direction\":\"response\",\"id\":" << record.id
         << ",\"url\":\"" << util::JsonEscape(record.url)
         << "\",\"status\":" << record.response_status
         << ",\"url_request_status\":" << record.url_request_status
         << ",\"received_length\":" << record.received_length
         << ",\"mime\":\"" << util::JsonEscape(record.mime)
         << "\",\"headers\":" << HeadersToJson(record.headers);
  }

  WriteBodyJson(line, record.body);
  line << ",\"truncated\":" << (record.truncated ? "true" : "false")
       << ",\"queue_truncated\":"
       << (record.queue_truncated ? "true" : "false") << "}\n";
  AppendLine(line.str());
}

void NetworkCapture::AppendLine(const std::string& line) {
  std::lock_guard lock(file_mutex_);
  const auto path = capture_dir_ /
                    util::Utf8ToWide("network-" + util::LocalDate() + ".jsonl");
  util::AppendTextFile(path, line);
}
