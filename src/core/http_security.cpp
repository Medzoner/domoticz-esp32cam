#include "core/http_security.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app::core::security {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

std::size_t base64Encode(const char* input, std::size_t length, char* output,
                         std::size_t capacity) {
  if (output == nullptr || capacity == 0) {
    return 0;
  }
  if (input == nullptr && length > 0) {
    return 0;
  }
  std::size_t written = 0;
  for (std::size_t index = 0; index < length; index += 3) {
    unsigned int block = static_cast<unsigned char>(input[index]) << 16;
    if (index + 1 < length) {
      block |= static_cast<unsigned char>(input[index + 1]) << 8;
    }
    if (index + 2 < length) {
      block |= static_cast<unsigned char>(input[index + 2]);
    }
    if (written + 5 > capacity) {
      return 0;
    }
    output[written++] = kBase64Alphabet[(block >> 18) & 0x3F];
    output[written++] = kBase64Alphabet[(block >> 12) & 0x3F];
    output[written++] =
        index + 1 < length ? kBase64Alphabet[(block >> 6) & 0x3F] : '=';
    output[written++] = index + 2 < length ? kBase64Alphabet[block & 0x3F] : '=';
  }
  output[written] = '\0';
  return written;
}

bool constantTimeEquals(const char* left, std::size_t leftLength,
                        const char* right, std::size_t rightLength) {
  if (left == nullptr || right == nullptr || leftLength != rightLength) {
    return false;
  }
  unsigned char difference = 0;
  for (std::size_t index = 0; index < leftLength; ++index) {
    difference |= static_cast<unsigned char>(left[index]) ^
                  static_cast<unsigned char>(right[index]);
  }
  return difference == 0;
}

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
