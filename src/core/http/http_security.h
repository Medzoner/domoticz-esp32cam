#pragma once

namespace app::core::security {

// Strict decimal integer parsing: rejects empty input, trailing characters and
// out of range values, unlike atoi() which silently turns "abc" into 0.
bool parseInteger(const char* text, int& value);

// Validates that a browser request comes from the camera itself. `origin` and
// `referer` may be null when the caller is not a browser; such callers are
// accepted because they can only reach the camera from the local network.
bool isSameOrigin(const char* origin, const char* referer, const char* host);

}  // namespace app::core::security
