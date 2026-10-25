#include <unity.h>

#include "core/service/service_startup.h"

namespace {
using app::core::ServiceStartup;
using app::core::ServiceState;

void startsAsNotStarted() {
  ServiceStartup startup;
  TEST_ASSERT_EQUAL_STRING("not_started",
                          app::core::serviceStateName(startup.state()));
  TEST_ASSERT_FALSE(startup.ready());
}

void creationIsNotReadiness() {
  ServiceStartup startup;
  TEST_ASSERT_TRUE(startup.start([] { return true; }));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceState::kStarting),
                        static_cast<int>(startup.state()));
  TEST_ASSERT_TRUE(startup.ready());
  TEST_ASSERT_EQUAL_STRING("running", app::core::serviceStateName(startup.state()));
}

void creationFailureIsSticky() {
  ServiceStartup startup;
  TEST_ASSERT_FALSE(startup.start([] { return false; }));
  unsigned calls = 0;
  TEST_ASSERT_FALSE(startup.start([&calls] {
    ++calls;
    return true;
  }));
  TEST_ASSERT_EQUAL_UINT(0, calls);
  TEST_ASSERT_FALSE(startup.ready());
  TEST_ASSERT_EQUAL_STRING("failed", app::core::serviceStateName(startup.state()));
}

void repeatedStartsDoNotDuplicateTasks() {
  ServiceStartup startup;
  unsigned calls = 0;
  const auto create = [&calls] {
    ++calls;
    return true;
  };
  TEST_ASSERT_TRUE(startup.start(create));
  TEST_ASSERT_TRUE(startup.start(create));
  TEST_ASSERT_TRUE(startup.ready());
  TEST_ASSERT_TRUE(startup.start(create));
  TEST_ASSERT_EQUAL_UINT(1, calls);
}

void immediateWorkerReadinessIsPreserved() {
  ServiceStartup startup;
  TEST_ASSERT_TRUE(startup.start([&startup] {
    startup.ready();
    return true;
  }));
  TEST_ASSERT_EQUAL_STRING("running", app::core::serviceStateName(startup.state()));
}

void workerFailureIsNotOverwrittenByCreationSuccess() {
  ServiceStartup startup;
  TEST_ASSERT_FALSE(startup.start([&startup] {
    startup.fail();
    return true;
  }));
  TEST_ASSERT_FALSE(startup.ready());
  TEST_ASSERT_EQUAL_STRING("failed", app::core::serviceStateName(startup.state()));
}

void listenerCanFailAfterTaskCreation() {
  ServiceStartup startup;
  TEST_ASSERT_TRUE(startup.start([] { return true; }));
  startup.fail();
  TEST_ASSERT_FALSE(startup.ready());
  TEST_ASSERT_FALSE(startup.start([] { return true; }));
  TEST_ASSERT_EQUAL_STRING("failed", app::core::serviceStateName(startup.state()));
}
}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(startsAsNotStarted);
  RUN_TEST(creationIsNotReadiness);
  RUN_TEST(creationFailureIsSticky);
  RUN_TEST(repeatedStartsDoNotDuplicateTasks);
  RUN_TEST(immediateWorkerReadinessIsPreserved);
  RUN_TEST(workerFailureIsNotOverwrittenByCreationSuccess);
  RUN_TEST(listenerCanFailAfterTaskCreation);
  return UNITY_END();
}
