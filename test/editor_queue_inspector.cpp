#include "editor/EditorQueueInspector.h"

#include "orders/CommandQueue.h"
#include "production/Production.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.queue.inspector_copies_orders_without_borrowing_runtime_storage") {
    eve::orders::CommandQueue queue;
    auto move = queue.append("move", 5, 10.0);
    auto wait = queue.append("wait", 1, 0.0);
    REQUIRE(move.value());
    REQUIRE(wait.value());
    RuntimeQueueInspector inspector;
    auto snapshot = inspector.capture(queue);
    CHECK_EQ(snapshot.domain, std::string("orders"));
    CHECK_EQ(snapshot.items.size(), 2U);
    CHECK(!snapshot.events.empty());
    auto filtered = inspector.capture(queue, "active", "move");
    CHECK_EQ(filtered.items.size(), 1U);
    queue.clear();
    CHECK_EQ(snapshot.items.size(), 2U);
}

TEST_CASE("editor.queue.inspector_filters_production_owner_and_state") {
    eve::production::WorkQueue queue;
    auto task = queue.enqueue("factory", "craft", "sword", eve::Value(eve::Value::Object{}), 3.0, 4);
    REQUIRE(task.value());
    RuntimeQueueInspector inspector;
    auto snapshot = inspector.capture(queue, "factory", "running", "craft");
    CHECK_EQ(snapshot.domain, std::string("production"));
    CHECK_EQ(snapshot.items.size(), 1U);
    CHECK_EQ(snapshot.items.front().product, std::string("sword"));
    CHECK_EQ(snapshot.items.front().duration, 3.0);
    CHECK(snapshot.diagnostics.empty());
}
