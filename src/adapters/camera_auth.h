#pragma once

#include "ports/authentication_port.h"

namespace app::adapters {

class CameraAuth final : public ports::AuthenticationPort {
 public:
  bool begin() override;
  bool enabled() const override;
  ports::AuthenticationDecision authorize(const char* authorization,
                                            const char* cookie) const override;
  const char* sessionCookie() const override;

 private:
  bool enabled_ = false;
  char sessionToken_[33]{};
  char sessionCookie_[128]{};
};

}  // namespace app::adapters
