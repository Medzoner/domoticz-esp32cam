#pragma once

#include <esp_http_server.h>

#include <cstdint>

#include "ports/auth/authentication_port.h"

namespace app::adapters::http {

enum class AccessPolicy {
  kRead,
  kMutation,
};

class HttpMiddleware {
 public:
  explicit HttpMiddleware(ports::AuthenticationPort& authentication);
  bool handle(httpd_req_t* request, AccessPolicy policy);

 private:
  bool authenticate(httpd_req_t* request);
  bool acceptMutation(httpd_req_t* request);
  static void log(httpd_req_t* request);

  ports::AuthenticationPort& authentication_;
  uint32_t lastMutationAt_ = 0;
};

}  // namespace app::adapters::http
