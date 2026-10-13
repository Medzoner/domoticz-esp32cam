#include "core/http_security.h"

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app::core::security {
namespace {

char lowerAscii(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character - 'A' + 'a');
  }
  return character;
}

bool equalsIgnoreCase(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  std::size_t index = 0;
  while (left[index] != '\0' && right[index] != '\0') {
    if (lowerAscii(left[index]) != lowerAscii(right[index])) {
      return false;
    }
    ++index;
  }
  return left[index] == '\0' && right[index] == '\0';
}

// Copies "scheme://authority" from an origin or referer value.
void copyOrigin(const char* value, char* output, std::size_t capacity) {
  if (capacity == 0) {
    return;
  }
  output[0] = '\0';
  if (value == nullptr) {
    return;
  }
  const char* scheme = std::strstr(value, "://");
  if (scheme == nullptr) {
    return;
  }
  const char* authority = scheme + 3;
  const char* end = std::strchr(authority, '/');
  const std::size_t authorityLength =
      end == nullptr ? std::strlen(authority)
                     : static_cast<std::size_t>(end - authority);
  const std::size_t schemeLength = static_cast<std::size_t>(scheme - value) + 3;
  const std::size_t total = schemeLength + authorityLength;
  if (total + 1 > capacity) {
    return;
  }
  std::memcpy(output, value, schemeLength);
  std::memcpy(output + schemeLength, authority, authorityLength);
  output[total] = '\0';
}

// Removes a trailing ":80" so that browsers that spell the default port
// explicitly still match the Host header.
void stripDefaultHttpPort(char* value) {
  const std::size_t length = std::strlen(value);
  const char kDefaultPortSuffix[] = ":80";
  const std::size_t suffixLength = sizeof(kDefaultPortSuffix) - 1;
  if (length > suffixLength &&
      std::strcmp(value + length - suffixLength, kDefaultPortSuffix) == 0) {
    value[length - suffixLength] = '\0';
  }
}

}  // namespace

bool parseInteger(const char* text, int& value) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const long parsed = std::strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') {
    return false;
  }
  if (parsed < INT_MIN || parsed > INT_MAX) {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

bool isSameOrigin(const char* origin, const char* referer, const char* host) {
  if (host == nullptr || host[0] == '\0') {
    return false;
  }

  char expected[80];
  if (std::strlen(host) + 8 > sizeof(expected)) {
    return false;
  }
  std::snprintf(expected, sizeof(expected), "http://%s", host);
  stripDefaultHttpPort(expected);

  const char* source = nullptr;
  if (origin != nullptr && origin[0] != '\0') {
    source = origin;
  } else if (referer != nullptr && referer[0] != '\0') {
    source = referer;
  } else {
    // No Origin and no Referer: the caller is not a browser running our code
    // in another origin, so there is no cross-site request to reject.
    return true;
  }

  char candidate[80];
  copyOrigin(source, candidate, sizeof(candidate));
  if (candidate[0] == '\0') {
    return false;
  }
  stripDefaultHttpPort(candidate);
  return equalsIgnoreCase(candidate, expected);
}

}  // namespace app::core::security
