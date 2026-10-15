#include "adapters/http/http_request.h"

#include <cstring>

#include "core/http_security.h"

namespace app::adapters::http::request {
namespace {

constexpr std::size_t kQueryCapacity = 96;
constexpr std::size_t kIntegerCapacity = 16;

}  // namespace

std::size_t copyHeader(httpd_req_t* request, const char* name, char* destination,
                       std::size_t capacity) {
  if (destination == nullptr || capacity == 0) {
    return 0;
  }
  destination[0] = '\0';
  if (httpd_req_get_hdr_value_str(request, name, destination, capacity) !=
      ESP_OK) {
    destination[0] = '\0';
    return 0;
  }
  return std::strlen(destination);
}

bool queryValue(httpd_req_t* request, const char* key, char* destination,
                std::size_t capacity) {
  char query[kQueryCapacity]{};
  return destination != nullptr && capacity > 0 &&
         httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
         httpd_query_key_value(query, key, destination, capacity) == ESP_OK;
}

bool queryInteger(httpd_req_t* request, const char* key, int& value) {
  char encoded[kIntegerCapacity]{};
  return queryValue(request, key, encoded, sizeof(encoded)) &&
         core::security::parseInteger(encoded, value);
}

}  // namespace app::adapters::http::request
