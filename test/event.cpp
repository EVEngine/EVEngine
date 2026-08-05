#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "event/Event.h"

TEST_CASE("Event.quitMessageSurvivesPumpPoll") {
    auto* ev = eve::event::Event::create();
    ev->push(new eve::event::Message("quit"));
    auto* msg = ev->poll();
    REQUIRE(msg != nullptr);
    CHECK(msg->name == "quit");
    delete msg;
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("Event.pollNameReturnsEmptyWhenEmpty") {
    auto* ev = eve::event::Event::create();
    CHECK(ev->pollName() == "");
}
