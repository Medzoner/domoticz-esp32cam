#pragma once

#include <cstddef>

namespace app::core::security {

// Encodes `length` bytes of `input` as base64. Returns the number of
// characters written, or 0 when the output buffer is too small.
std::size_t base64Encode(const char* input, std::size_t length, char* output,
                         std::size_t capacity);

// Compares two buffers without leaking where they first differ.
bool constantTimeEquals(const char* left, std::size_t leftLength,
                        const char* right, std::size_t rightLength);

// Strict decimal integer parsing: rejects empty input, trailing characters and
// out of range values, unlike atoi() which silently turns "abc" into 0.
bool parseInteger(const char* text, int& value);

// Validates that a browser request comes from the camera itself. `origin` and
// `referer` may be null when the caller is not a browser; such callers are
// accepted because they can only reach the camera from the local network.
bool isSameOrigin(const char* origin, const char* referer, const char* host);

}  // namespace app::core::security
