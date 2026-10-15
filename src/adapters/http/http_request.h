#pragma once

#include <esp_http_server.h>

#include <cstddef>

namespace app::adapters::http::request {

std::size_t copyHeader(httpd_req_t* request, const char* name, char* destination,
                       std::size_t capacity);
bool queryValue(httpd_req_t* request, const char* key, char* destination,
                std::size_t capacity);
bool queryInteger(httpd_req_t* request, const char* key, int& value);

}  // namespace app::adapters::http::request
