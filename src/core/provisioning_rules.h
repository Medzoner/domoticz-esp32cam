#pragma once

#include <cstddef>
#include <cstdint>

namespace app::core::provisioning {

// Maximum sizes defined by the WPA2 standard: a 32 byte SSID and an
// 8..63 character passphrase (or a 64 digit hexadecimal pre-shared key).
inline constexpr std::size_t kMaximumSsidBytes = 32;
inline constexpr std::size_t kSsidCapacity = kMaximumSsidBytes + 1;
inline constexpr std::size_t kMaximumPassphraseBytes = 64;
inline constexpr std::size_t kPasswordCapacity = kMaximumPassphraseBytes + 1;
inline constexpr std::size_t kMaximumFormBodyBytes = 512;

// Default values shipped in include/secrets.example.h. They mean that nobody
// has provided real credentials for this camera yet.
inline constexpr char kUnconfiguredSsid[] = "your-wifi";
inline constexpr char kUnconfiguredPassword[] = "your-password";

enum class ParseStatus : std::uint8_t {
  kOk = 0,
  kBodyTooLong,
  kMalformed,
  kMissingSsid,
  kInvalidSsid,
  kInvalidPassword,
};

struct Request {
  char ssid[kSsidCapacity]{};
  char password[kPasswordCapacity]{};
  bool useDhcp = true;
};

enum class BootMode : std::uint8_t { kStation, kProvisioning };

// Decodes one application/x-www-form-urlencoded component. Returns the number
// of bytes written, or 0 when the input is malformed or does not fit.
std::size_t urlDecode(const char* input, std::size_t length, char* output,
                      std::size_t capacity);

// Parses a form body such as "ssid=My+Net&pass=secret&dhcp=1".
ParseStatus parseFormBody(const char* body, std::size_t length, Request& request);

bool isValidSsid(const char* ssid, std::size_t length);

// An empty password is valid: it selects an open network.
bool isValidPassword(const char* password, std::size_t length);

// True for operating-system captive portal probes such as /generate_204.
bool isCaptivePortalProbe(const char* path);

// True while secrets.h still holds the example placeholder.
bool isUnconfigured(const char* ssid);

BootMode decideBootMode(bool hasCredentials, bool connected);

const char* describeParseStatus(ParseStatus status);

}  // namespace app::core::provisioning
