#pragma once

#include <esp_http_server.h>

#include <cstdint>

#include "adapters/mjpeg_server.h"
#include "ports/camera_port.h"

namespace app::adapters {

// Outcome of the HTTP access checks. Kept at namespace scope so that both the
// declarations and the out of line definitions can name it unambiguously.
enum class HttpAccess {
  kGranted,
  kCrossOrigin,
  kRateLimited,
};

class HttpControlServer {
 public:
  HttpControlServer(ports::CameraPort& camera, MjpegServer& streams);
  ~HttpControlServer() = default;
  HttpControlServer(const HttpControlServer&) = delete;
  HttpControlServer& operator=(const HttpControlServer&) = delete;

  bool begin();

 private:
  static esp_err_t indexHandler(httpd_req_t* request);
  static esp_err_t healthHandler(httpd_req_t* request);
  static esp_err_t statusHandler(httpd_req_t* request);
  static esp_err_t captureHandler(httpd_req_t* request);
  static esp_err_t controlHandler(httpd_req_t* request);
  static esp_err_t stopStreamsHandler(httpd_req_t* request);

  // Access checks. They never write to the socket: the caller sends the
  // matching rejection through denyAccess() so that a response is only ever
  // produced once.
  static HttpAccess mutationAccess(httpd_req_t* request);
  static esp_err_t denyAccess(httpd_req_t* request, HttpAccess access);

  static HttpControlServer& owner(httpd_req_t* request);
  static void setCommonHeaders(httpd_req_t* request);
  // Rejects a request with an explicit status line. `status` and `message`
  // must outlive the call (string literals are fine).
  static esp_err_t reject(httpd_req_t* request, const char* status,
                          const char* message);
  static httpd_uri_t makeUri(const char* path,
                             esp_err_t (*handler)(httpd_req_t*),
                             HttpControlServer* server,
                             httpd_method_t method = HTTP_GET);

  ports::CameraPort& camera_;
  MjpegServer& streams_;
  httpd_handle_t server_ = nullptr;
  uint32_t lastMutationAt_ = 0;
};

}  // namespace app::adapters
