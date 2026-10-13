#include "core/provisioning_rules.h"

#include <cstring>

namespace app::core::provisioning {
namespace {

int hexValue(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

char lowerAscii(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character - 'A' + 'a');
  }
  return character;
}

bool equalsIgnoreCaseAscii(const char* left, const char* right) {
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

bool isTruthyFlag(const char* value) {
  return equalsIgnoreCaseAscii(value, "1") ||
         equalsIgnoreCaseAscii(value, "true") ||
         equalsIgnoreCaseAscii(value, "on") ||
         equalsIgnoreCaseAscii(value, "yes");
}

bool isFalsyFlag(const char* value) {
  return equalsIgnoreCaseAscii(value, "0") ||
         equalsIgnoreCaseAscii(value, "false") ||
         equalsIgnoreCaseAscii(value, "off") ||
         equalsIgnoreCaseAscii(value, "no") || value[0] == '\0';
}

bool hasOnlyPrintableBytes(const char* value, std::size_t length) {
  for (std::size_t index = 0; index < length; ++index) {
    const auto byte = static_cast<unsigned char>(value[index]);
    if (byte < 0x20 || byte == 0x7F) {
      return false;
    }
  }
  return true;
}

bool isHexadecimal(const char* value, std::size_t length) {
  for (std::size_t index = 0; index < length; ++index) {
    if (hexValue(value[index]) < 0) {
      return false;
    }
  }
  return true;
}

// Paths that operating systems request while deciding whether a captive
// portal is present. Answering them redirects the system browser to the
// onboarding page.
constexpr const char* kCaptivePortalProbes[] = {
    "/generate_204",
    "/gen_204",
    "/hotspot-detect.html",
    "/library/test/success.html",
    "/ncsi.txt",
    "/connecttest.txt",
    "/fwlink",
    "/success.txt",
    "/canonical.html",
    "/kindle-wifi/wifistub.html",
};

}  // namespace

std::size_t urlDecode(const char* input, std::size_t length, char* output,
                      std::size_t capacity) {
  if (input == nullptr || output == nullptr || capacity == 0) {
    return 0;
  }
  std::size_t written = 0;
  for (std::size_t index = 0; index < length; ++index) {
    char character = input[index];
    if (character == '+') {
      character = ' ';
    } else if (character == '%') {
      if (index + 2 >= length) {
        return 0;
      }
      const int high = hexValue(input[index + 1]);
      const int low = hexValue(input[index + 2]);
      if (high < 0 || low < 0) {
        return 0;
      }
      character = static_cast<char>((high << 4) | low);
      index += 2;
    }
    if (written + 1 >= capacity) {
      return 0;
    }
    output[written++] = character;
  }
  output[written] = '\0';
  return written;
}

bool isValidSsid(const char* ssid, std::size_t length) {
  if (ssid == nullptr || length == 0 || length > kMaximumSsidBytes) {
    return false;
  }
  return hasOnlyPrintableBytes(ssid, length);
}

bool isValidPassword(const char* password, std::size_t length) {
  if (password == nullptr || length > kMaximumPassphraseBytes) {
    return false;
  }
  if (length == 0) {
    return true;  // open network
  }
  if (!hasOnlyPrintableBytes(password, length)) {
    return false;
  }
  if (length >= 8 && length <= 63) {
    return true;
  }
  // A 64 digit hexadecimal value is a raw WPA2 pre-shared key.
  return length == 64 && isHexadecimal(password, length);
}

ParseStatus parseFormBody(const char* body, std::size_t length,
                          Request& request) {
  request = Request{};
  if (body == nullptr) {
    return ParseStatus::kMalformed;
  }
  if (length > kMaximumFormBodyBytes) {
    return ParseStatus::kBodyTooLong;
  }

  bool seenSsid = false;
  char key[24]{};
  // A 64 character secret needs at most 192 bytes once every byte is encoded.
  char value[192]{};
  std::size_t offset = 0;

  while (offset < length) {
    std::size_t segmentLength = 0;
    while (offset + segmentLength < length &&
           body[offset + segmentLength] != '&') {
      ++segmentLength;
    }
    const char* segment = body + offset;

    if (segmentLength > 0) {
      const void* separator = std::memchr(segment, '=', segmentLength);
      const std::size_t keyLength =
          separator == nullptr
              ? segmentLength
              : static_cast<std::size_t>(
                    static_cast<const char*>(separator) - segment);
      const char* rawValue =
          separator == nullptr ? segment + segmentLength
                               : static_cast<const char*>(separator) + 1;
      const std::size_t rawValueLength =
          separator == nullptr ? 0 : segmentLength - keyLength - 1;

      if (keyLength > 0 && keyLength < sizeof(key)) {
        std::memcpy(key, segment, keyLength);
        key[keyLength] = '\0';
        value[0] = '\0';
        if (rawValueLength > 0) {
          const bool decoded =
              urlDecode(rawValue, rawValueLength, value, sizeof(value)) > 0;
          if (!decoded) {
            if (std::strcmp(key, "ssid") == 0) {
              return ParseStatus::kInvalidSsid;
            }
            if (std::strcmp(key, "pass") == 0 ||
                std::strcmp(key, "password") == 0) {
              return ParseStatus::kInvalidPassword;
            }
            return ParseStatus::kMalformed;
          }
        }

        if (std::strcmp(key, "ssid") == 0) {
          const std::size_t valueLength = std::strlen(value);
          if (valueLength >= sizeof(request.ssid)) {
            return ParseStatus::kInvalidSsid;
          }
          std::memcpy(request.ssid, value, valueLength + 1);
          seenSsid = true;
        } else if (std::strcmp(key, "pass") == 0 ||
                   std::strcmp(key, "password") == 0) {
          const std::size_t valueLength = std::strlen(value);
          if (valueLength >= sizeof(request.password)) {
            return ParseStatus::kInvalidPassword;
          }
          std::memcpy(request.password, value, valueLength + 1);
        } else if (std::strcmp(key, "dhcp") == 0) {
          if (isTruthyFlag(value)) {
            request.useDhcp = true;
          } else if (isFalsyFlag(value)) {
            request.useDhcp = false;
          } else {
            return ParseStatus::kMalformed;
          }
        }
      }
    }

    offset += segmentLength;
    if (offset < length && body[offset] == '&') {
      ++offset;
    }
  }

  if (!seenSsid) {
    return ParseStatus::kMissingSsid;
  }
  if (!isValidSsid(request.ssid, std::strlen(request.ssid))) {
    return ParseStatus::kInvalidSsid;
  }
  if (!isValidPassword(request.password, std::strlen(request.password))) {
    return ParseStatus::kInvalidPassword;
  }
  return ParseStatus::kOk;
}

bool isCaptivePortalProbe(const char* path) {
  if (path == nullptr) {
    return false;
  }
  char normalized[64]{};
  std::size_t length = 0;
  for (const char* cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '?') {
      break;
    }
    if (length + 1 >= sizeof(normalized)) {
      return false;
    }
    normalized[length++] = *cursor;
  }
  while (length > 1 && normalized[length - 1] == '/') {
    --length;
  }
  normalized[length] = '\0';
  if (length == 0) {
    return false;
  }
  for (const char* probe : kCaptivePortalProbes) {
    if (equalsIgnoreCaseAscii(normalized, probe)) {
      return true;
    }
  }
  return false;
}

bool isUnconfigured(const char* ssid) {
  return ssid == nullptr || ssid[0] == '\0' ||
         std::strcmp(ssid, kUnconfiguredSsid) == 0;
}

BootMode decideBootMode(bool hasCredentials, bool connected) {
  return hasCredentials && connected ? BootMode::kStation
                                     : BootMode::kProvisioning;
}

const char* describeParseStatus(ParseStatus status) {
  switch (status) {
    case ParseStatus::kOk:
      return "ok";
    case ParseStatus::kBodyTooLong:
      return "the submitted form is too large";
    case ParseStatus::kMalformed:
      return "the submitted form could not be read";
    case ParseStatus::kMissingSsid:
      return "a network name is required";
    case ParseStatus::kInvalidSsid:
      return "network names must be 1 to 32 characters";
    case ParseStatus::kInvalidPassword:
      return "passwords must be empty, 8 to 63 characters, or 64 hex digits";
  }
  return "invalid request";
}

}  // namespace app::core::provisioning
