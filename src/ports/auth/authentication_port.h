#pragma once

namespace app::ports {

struct AuthenticationDecision {
  bool granted;
  bool issueSession;
};

class AuthenticationPort {
 public:
  virtual ~AuthenticationPort() = default;
  virtual bool begin() = 0;
  virtual bool enabled() const = 0;
  virtual AuthenticationDecision authorize(const char* authorization,
                                            const char* cookie) const = 0;
  virtual const char* sessionCookie() const = 0;
};

}  // namespace app::ports
