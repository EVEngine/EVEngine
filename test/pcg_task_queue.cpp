#include "procgen/PcgTaskQueue.h"
#include <zeroerr/unittest.h>
using namespace eve::procgen;
TEST_CASE("Pcg task queue waits then publishes a task without invoking callbacks") {
    PcgTaskQueue q; auto id=q.add(); REQUIRE(id.ok());
    auto state=q.tick(0.24); REQUIRE(state.ok()); CHECK(state.value()==PcgTaskQueueStatus::Waiting);
    state=q.tick(0.01); REQUIRE(state.ok()); CHECK(state.value()==PcgTaskQueueStatus::Ready);
    CHECK(q.readyTaskId()==id.value()); REQUIRE(q.resolveReady(true).ok()); CHECK(q.queueSize()==0);
}
TEST_CASE("Pcg task queue repeats unfinished tasks in queue order") {
    PcgTaskQueue q; auto a=q.add(0); auto b=q.add(0); REQUIRE(a.ok()); REQUIRE(b.ok());
    REQUIRE(q.tick(0).ok()); CHECK(q.readyTaskId()==a.value()); REQUIRE(q.resolveReady(false).ok());
    REQUIRE(q.tick(0).ok()); CHECK(q.readyTaskId()==b.value()); REQUIRE(q.resolveReady(true).ok());
    REQUIRE(q.tick(0).ok()); CHECK(q.readyTaskId()==a.value());
}
TEST_CASE("Pcg task queue validates time and cancels atomically") {
    PcgTaskQueue q; CHECK(!q.add(-1).ok()); REQUIRE(q.add(1).ok());
    CHECK(!q.tick(-1).ok()); CHECK(q.queueSize()==1);
    CHECK(q.cancelAll()==PcgTaskQueueStatus::Idle); CHECK(q.queueSize()==0);
}
