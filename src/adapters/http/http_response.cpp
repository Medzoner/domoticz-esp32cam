#include "adapters/http/http_response.h"

namespace app::adapters::http::response {
namespace {

constexpr char kTextContentType[] = "text/plain; charset=utf-8";
constexpr char kCacheControl[] = "no-store, no-cache, must-revalidate";
constexpr char kStatusUnauthorized[] = "401 Unauthorized";
constexpr char kAuthenticateValue[] =
    "Basic realm=\"ESP32-CAM\", charset=\"UTF-8\"";
constexpr char kMessageUnauthorized[] = "authentication required";

}  // namespace

void setSecurityHeaders(httpd_req_t* request) {
  // Deliberately no permissive CORS header. Browser mutations are additionally
  // checked by HttpMiddleware against the camera's own origin.
  httpd_resp_set_hdr(request, "Cache-Control", kCacheControl);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Referrer-Policy", "same-origin");
}

esp_err_t error(httpd_req_t* request, const char* status, const char* message) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, kTextContentType);
  setSecurityHeaders(request);
  return httpd_resp_send(request, message, HTTPD_RESP_USE_STRLEN);
}

esp_err_t json(httpd_req_t* request, const char* body) {
  httpd_resp_set_type(request, kJsonContentType);
  setSecurityHeaders(request);
  return httpd_resp_sendstr(request, body);
}

esp_err_t empty(httpd_req_t* request) {
  setSecurityHeaders(request);
  return httpd_resp_send(request, nullptr, 0);
}

esp_err_t unauthorized(httpd_req_t* request) {
  httpd_resp_set_hdr(request, "WWW-Authenticate", kAuthenticateValue);
  return error(request, kStatusUnauthorized, kMessageUnauthorized);
}

}  // namespace app::adapters::http::response
