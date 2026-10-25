#pragma once

#include <atomic>

namespace app::core {

enum class ServiceState { kNotStarted, kStarting, kRunning, kFailed };

inline const char* serviceStateName(ServiceState state) {
  switch (state) {
    case ServiceState::kNotStarted:
      return "not_started";
    case ServiceState::kStarting:
      return "starting";
    case ServiceState::kRunning:
      return "running";
    case ServiceState::kFailed:
      return "failed";
  }
  return "failed";
}

// One startup attempt per instance. Creation success means scheduled, not ready.
// The worker publishes readiness only after its listener successfully starts.
// Failure is sticky until reboot; repeated begin calls cannot duplicate tasks.
class ServiceStartup {
 public:
  template <typename Create>
  bool start(Create create) {
    ServiceState expected = ServiceState::kNotStarted;
    if (!state_.compare_exchange_strong(expected, ServiceState::kStarting)) {
      return expected != ServiceState::kFailed;
    }
    if (!create()) {
      fail();
      return false;
    }
    // Worker may already be running or failed by the time create returns.
    return state() != ServiceState::kFailed;
  }

  bool ready() {
    ServiceState expected = ServiceState::kStarting;
    return state_.compare_exchange_strong(expected, ServiceState::kRunning);
  }

  void fail() { state_ = ServiceState::kFailed; }
  ServiceState state() const { return state_.load(); }

 private:
  std::atomic<ServiceState> state_{ServiceState::kNotStarted};
};

}  // namespace app::core
