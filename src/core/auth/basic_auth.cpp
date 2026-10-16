#include "core/auth/basic_auth.h"

#include <cctype>
#include <cstdint>
#include <cstring>

namespace app::core::auth {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kBasicPrefix[] = "Basic ";
constexpr char kSessionCookieName[] = "camera_session=";

bool startsWithIgnoreCase(const char* value, const char* prefix) {
  if (value == nullptr || prefix == nullptr) {
    return false;
  }
  while (*prefix != '\0') {
    if (*value == '\0' ||
        std::tolower(static_cast<unsigned char>(*value)) !=
            std::tolower(static_cast<unsigned char>(*prefix))) {
      return false;
    }
    ++value;
    ++prefix;
  }
  return true;
}

void copyHeaderValue(const char* source, char* destination,
                     std::size_t capacity) {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  while (*source == ' ' || *source == '\t') {
    ++source;
  }
  std::size_t length = std::strlen(source);
  while (length > 0 &&
         (source[length - 1] == ' ' || source[length - 1] == '\t')) {
    --length;
  }
  if (length >= capacity) {
    destination[0] = '\0';
    return;
  }
  std::memcpy(destination, source, length);
  destination[length] = '\0';
}

bool constantTimeEquals(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  const std::size_t leftLength = std::strlen(left);
  const std::size_t rightLength = std::strlen(right);
  const std::size_t maximum =
      leftLength > rightLength ? leftLength : rightLength;
  unsigned int difference = static_cast<unsigned int>(leftLength ^ rightLength);
  for (std::size_t index = 0; index < maximum; ++index) {
    const unsigned char leftValue =
        index < leftLength ? static_cast<unsigned char>(left[index]) : 0;
    const unsigned char rightValue =
        index < rightLength ? static_cast<unsigned char>(right[index]) : 0;
    difference |= leftValue ^ rightValue;
  }
  return difference == 0;
}

}  // namespace

bool isConfigurationValid(const char* username, const char* password) {
  if (username == nullptr || password == nullptr) {
    return false;
  }
  const bool hasUsername = username[0] != '\0';
  const bool hasPassword = password[0] != '\0';
  if (!hasUsername && !hasPassword) {
    return true;
  }
  return hasUsername && hasPassword && std::strchr(username, ':') == nullptr &&
         std::strlen(username) + std::strlen(password) <= 96;
}

bool isEnabled(const char* username, const char* password) {
  return isConfigurationValid(username, password) && username[0] != '\0';
}

std::size_t formatBasicAuthorization(char* destination, std::size_t capacity,
                                     const char* username,
                                     const char* password) {
  if (destination == nullptr || capacity == 0 ||
      !isEnabled(username, password)) {
    return 0;
  }
  const std::size_t usernameLength = std::strlen(username);
  const std::size_t passwordLength = std::strlen(password);
  char credentials[98]{};
  std::memcpy(credentials, username, usernameLength);
  credentials[usernameLength] = ':';
  std::memcpy(credentials + usernameLength + 1, password, passwordLength);
  const std::size_t credentialsLength = usernameLength + passwordLength + 1;
  const std::size_t encodedLength = ((credentialsLength + 2) / 3) * 4;
  const std::size_t required = sizeof(kBasicPrefix) - 1 + encodedLength + 1;
  if (required > capacity) {
    destination[0] = '\0';
    return 0;
  }

  std::memcpy(destination, kBasicPrefix, sizeof(kBasicPrefix) - 1);
  std::size_t output = sizeof(kBasicPrefix) - 1;
  for (std::size_t index = 0; index < credentialsLength; index += 3) {
    const uint32_t first = static_cast<unsigned char>(credentials[index]);
    const uint32_t second = index + 1 < credentialsLength
                                ? static_cast<unsigned char>(credentials[index + 1])
                                : 0;
    const uint32_t third = index + 2 < credentialsLength
                               ? static_cast<unsigned char>(credentials[index + 2])
                               : 0;
    const uint32_t value = (first << 16) | (second << 8) | third;
    destination[output++] = kBase64Alphabet[(value >> 18) & 0x3f];
    destination[output++] = kBase64Alphabet[(value >> 12) & 0x3f];
    destination[output++] = index + 1 < credentialsLength
                                ? kBase64Alphabet[(value >> 6) & 0x3f]
                                : '=';
    destination[output++] = index + 2 < credentialsLength
                                ? kBase64Alphabet[value & 0x3f]
                                : '=';
  }
  destination[output] = '\0';
  return output;
}

bool isBasicAuthorizationValid(const char* authorization,
                               const char* username, const char* password) {
  char expected[160]{};
  return formatBasicAuthorization(expected, sizeof(expected), username,
                                  password) > 0 &&
         constantTimeEquals(authorization, expected);
}

bool hasSessionCookie(const char* cookieHeader, const char* sessionToken) {
  if (cookieHeader == nullptr || sessionToken == nullptr ||
      sessionToken[0] == '\0') {
    return false;
  }
  const std::size_t tokenLength = std::strlen(sessionToken);
  const char* cursor = cookieHeader;
  while (*cursor != '\0') {
    while (*cursor == ' ' || *cursor == ';') {
      ++cursor;
    }
    if (std::strncmp(cursor, kSessionCookieName,
                     sizeof(kSessionCookieName) - 1) == 0) {
      const char* value = cursor + sizeof(kSessionCookieName) - 1;
      return std::strncmp(value, sessionToken, tokenLength) == 0 &&
             (value[tokenLength] == '\0' || value[tokenLength] == ';' ||
              value[tokenLength] == ' ');
    }
    const char* separator = std::strchr(cursor, ';');
    if (separator == nullptr) {
      break;
    }
    cursor = separator + 1;
  }
  return false;
}

void captureHeader(const char* line, char* authorization,
                   std::size_t authorizationCapacity, char* cookie,
                   std::size_t cookieCapacity) {
  if (line == nullptr) {
    return;
  }
  constexpr char kAuthorizationHeader[] = "Authorization:";
  constexpr char kCookieHeader[] = "Cookie:";
  if (startsWithIgnoreCase(line, kAuthorizationHeader)) {
    copyHeaderValue(line + sizeof(kAuthorizationHeader) - 1, authorization,
                    authorizationCapacity);
  } else if (startsWithIgnoreCase(line, kCookieHeader)) {
    copyHeaderValue(line + sizeof(kCookieHeader) - 1, cookie, cookieCapacity);
  }
}

}  // namespace app::core::auth
