#pragma once

#include <esp_http_server.h>

#include "adapters/http/http_middleware.h"
#include "adapters/http/http_router.h"
#include "adapters/stream/mjpeg_server.h"
#include "ports/auth/authentication_port.h"
#include "ports/camera/camera_port.h"
#include "ports/recording/recording_port.h"

namespace app::adapters {

class HttpControlServer {
 public:
  HttpControlServer(ports::CameraPort& camera, MjpegServer& streams,
                    ports::RecordingPort& recordings,
                    ports::AuthenticationPort& authentication);
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
  static esp_err_t recordingStartHandler(httpd_req_t* request);
  static esp_err_t recordingStopHandler(httpd_req_t* request);
  static esp_err_t recordingsHandler(httpd_req_t* request);
  static esp_err_t recordingDeleteHandler(httpd_req_t* request);

  static HttpControlServer& owner(httpd_req_t* request);

  ports::CameraPort& camera_;
  MjpegServer& streams_;
  ports::RecordingPort& recordings_;
  http::HttpMiddleware middleware_;
  http::HttpRouter router_;
};

}  // namespace app::adapters
