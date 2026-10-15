#pragma once

#include <esp_http_server.h>

namespace app::adapters::http::response {

inline constexpr char kJsonContentType[] = "application/json";
inline constexpr char kHtmlContentType[] = "text/html; charset=utf-8";
inline constexpr char kJpegContentType[] = "image/jpeg";

inline constexpr char kStatusBadRequest[] = "400 Bad Request";
inline constexpr char kStatusConflict[] = "409 Conflict";
inline constexpr char kStatusInternalServerError[] = "500 Internal Server Error";
inline constexpr char kStatusServiceUnavailable[] = "503 Service Unavailable";

void setSecurityHeaders(httpd_req_t* request);
esp_err_t error(httpd_req_t* request, const char* status, const char* message);
esp_err_t json(httpd_req_t* request, const char* body);
esp_err_t empty(httpd_req_t* request);
esp_err_t unauthorized(httpd_req_t* request);

}  // namespace app::adapters::http::response
