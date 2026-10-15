#pragma once

#include <esp_http_server.h>

#include <cstddef>

namespace app::adapters::http {

struct Route {
  const char* path;
  httpd_method_t method;
  esp_err_t (*handler)(httpd_req_t*);
};

class HttpRouter {
 public:
  bool begin(void* context, const Route* routes, std::size_t routeCount);

 private:
  httpd_handle_t server_ = nullptr;
};

}  // namespace app::adapters::http
