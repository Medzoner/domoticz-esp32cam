#pragma once

#include <esp_http_server.h>

#include "adapters/mjpeg_server.h"
#include "ports/camera_port.h"

namespace app::adapters {

class HttpControlServer {
 public:
  HttpControlServer(ports::CameraPort& camera, MjpegServer& streams);
  bool begin();

 private:
  static esp_err_t indexHandler(httpd_req_t* request);
  static esp_err_t healthHandler(httpd_req_t* request);
  static esp_err_t statusHandler(httpd_req_t* request);
  static esp_err_t captureHandler(httpd_req_t* request);
  static esp_err_t controlHandler(httpd_req_t* request);
  static esp_err_t stopStreamsHandler(httpd_req_t* request);
  static void setCommonHeaders(httpd_req_t* request);
  static httpd_uri_t makeUri(const char* path,
                             esp_err_t (*handler)(httpd_req_t*),
                             HttpControlServer* server);

  ports::CameraPort& camera_;
  MjpegServer& streams_;
  httpd_handle_t server_ = nullptr;
};

}  // namespace app::adapters
