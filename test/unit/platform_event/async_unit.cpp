// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "async.cpp"
  ]
}
*/
// clang-format on

#include "platform_event/PlatformEvent.h"
#include "thread/Thread.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

// The thread API publishes an event into the platform queue. A poll consumes
// exactly one event and exposes its first string argument as lastData.
TEST_CASE("async.post_main.delivers_name_and_payload_once") {
    auto* events  = eve::platform_event::PlatformEvent::create();
    auto* threads = eve::thread::Thread::create();
    events->clear();

    threads->postMain("async.sample", "payload");
    CHECK_EQ(events->pollName(), std::string("async.sample"));
    CHECK_EQ(events->getLastData(), std::string("payload"));
    CHECK_EQ(events->pollName(), std::string(""));
    REQUIRE_EQ(events->getLastData(), std::string(""));
}

TEST_CASE("async.post_main.preserves_fifo_order_and_empty_payload") {
    auto* events  = eve::platform_event::PlatformEvent::create();
    auto* threads = eve::thread::Thread::create();
    events->clear();

    threads->postMain("first", "one");
    threads->postMain("second");
    threads->postMain("third", "three");

    CHECK_EQ(events->pollName(), std::string("first"));
    CHECK_EQ(events->getLastData(), std::string("one"));
    CHECK_EQ(events->pollName(), std::string("second"));
    CHECK_EQ(events->getLastData(), std::string(""));
    CHECK_EQ(events->pollName(), std::string("third"));
    CHECK_EQ(events->getLastData(), std::string("three"));
    REQUIRE_EQ(events->pollName(), std::string(""));
}
