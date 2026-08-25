#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <string>

using namespace eve::ui;

TEST_CASE("ui.control.policies_survive_flatten_and_reconcile") {
    UIHost *host = UIHost::createHost("control-semantics");
    REQUIRE(host != nullptr);

    WidgetDesc action = button("Apply", "apply")
                            .withEnabled(false)
                            .withFocusMode(FocusMode::Click)
                            .withMouseFilter(MouseFilter::Ignore)
                            .withTabIndex(7)
                            .withFocusOrder("cancel", "reset")
                            .withFocusNeighbors("left", "right", "up", "down")
                            .withAccessibility(AccessibilityRole::Button, "Apply changes",
                                               "Commits the current property edits");
    host->setTree(window("Controls", {std::move(action)}, "root"));

    UINode *node = host->findById("apply");
    REQUIRE(node != nullptr);
    CHECK(!node->enabled);
    CHECK_EQ(static_cast<int>(node->focusMode), static_cast<int>(FocusMode::Click));
    CHECK_EQ(static_cast<int>(node->mouseFilter), static_cast<int>(MouseFilter::Ignore));
    CHECK_EQ(node->tabIndex, 7);
    CHECK_EQ(node->focusPrevious, std::string("cancel"));
    CHECK_EQ(node->focusNext, std::string("reset"));
    CHECK_EQ(node->focusLeft, std::string("left"));
    CHECK_EQ(node->focusRight, std::string("right"));
    CHECK_EQ(node->focusUp, std::string("up"));
    CHECK_EQ(node->focusDown, std::string("down"));
    CHECK_EQ(static_cast<int>(node->accessibilityRole),
             static_cast<int>(AccessibilityRole::Button));
    CHECK_EQ(node->accessibilityName, std::string("Apply changes"));

    WidgetDesc updated = button("Apply now", "apply")
                             .withFocusMode(FocusMode::All)
                             .withMouseFilter(MouseFilter::Pass)
                             .withTabIndex(2)
                             .withAccessibility(AccessibilityRole::Button, "Apply now");
    CHECK(host->setTreeReconcile(window("Controls", {std::move(updated)}, "root")));
    node = host->findById("apply");
    REQUIRE(node != nullptr);
    CHECK(node->enabled);
    CHECK_EQ(static_cast<int>(node->focusMode), static_cast<int>(FocusMode::All));
    CHECK_EQ(static_cast<int>(node->mouseFilter), static_cast<int>(MouseFilter::Pass));
    CHECK_EQ(node->tabIndex, 2);
    CHECK(node->focusPrevious.empty());
    CHECK_EQ(node->accessibilityName, std::string("Apply now"));
}

TEST_CASE("ui.control.focus_request_rejects_ineligible_controls") {
    UIHost *host = UIHost::createHost("focus-semantics");
    REQUIRE(host != nullptr);
    host->setTree(window("Focus",
                         {button("First", "first"),
                          button("Disabled", "disabled").withEnabled(false),
                          button("Decorative", "decorative").withFocusMode(FocusMode::None)},
                         "root"));

    CHECK(!host->requestFocusById("missing"));
    CHECK(!host->requestFocusById("disabled"));
    CHECK(!host->requestFocusById("decorative"));
    CHECK(!host->requestFocusById("root"));
    CHECK(host->requestFocusById("first"));
    CHECK(host->findById("first")->focusRequested);

    host->findById("first")->focused = true;
    CHECK_EQ(host->focusedId(), std::string("first"));
    host->setEnabledById("first", false);
    CHECK(host->focusedId().empty());
    CHECK(!host->findById("first")->focusRequested);
}

TEST_CASE("ui.control.focus_navigation_uses_neighbors_then_stable_tab_order") {
    UIHost *host = UIHost::createHost("focus-navigation");
    REQUIRE(host != nullptr);
    host->setTree(window(
        "Focus",
        {text("Label", "label"),
         button("Click only", "click").withFocusMode(FocusMode::Click).withTabIndex(0),
         button("Disabled", "disabled").withEnabled(false).withTabIndex(5),
         button("Second", "second").withTabIndex(10),
         button("First", "first")
             .withTabIndex(20)
             .withFocusOrder("", "special")
             .withFocusNeighbors("second", "special", "second", "special"),
         button("Special", "special").withTabIndex(30).withFocusOrder("second", "")},
        "root"));

    // No current focus: select the lowest eligible tab index. Decorative,
    // disabled and click-only nodes do not participate in keyboard traversal.
    CHECK(host->moveFocus(FocusDirection::Next));
    CHECK(host->findById("second")->focusRequested);

    host->findById("second")->focused = true;
    CHECK(host->moveFocus(FocusDirection::Next));
    CHECK(host->findById("first")->focusRequested);

    host->findById("second")->focused = false;
    host->findById("first")->focused = true;
    CHECK(host->moveFocus(FocusDirection::Next));
    CHECK(host->findById("special")->focusRequested);

    host->findById("first")->focused = false;
    host->findById("special")->focused = true;
    CHECK(host->moveFocus(FocusDirection::Previous));
    CHECK(host->findById("second")->focusRequested);

    host->findById("special")->focused = false;
    host->findById("first")->focused = true;
    CHECK(host->moveFocus(FocusDirection::Left));
    CHECK(host->findById("second")->focusRequested);

    host->findById("first")->focused = false;
    host->findById("special")->focused = true;
    CHECK(!host->moveFocus(FocusDirection::Next, false));
}

TEST_CASE("ui.control.mouse_pass_bubbles_click_and_stop_contains_it") {
    int childClicks = 0;
    int ignoredClicks = 0;
    int rootClicks = 0;
    UIHost *host = UIHost::createHost("pointer-routing");
    REQUIRE(host != nullptr);

    WidgetDesc child = button("Action", "action", [&]() { ++childClicks; })
                           .withMouseFilter(MouseFilter::Pass);
    WidgetDesc ignored = group({std::move(child)}, "ignored")
                             .withClick([&]() { ++ignoredClicks; })
                             .withMouseFilter(MouseFilter::Ignore);
    WidgetDesc root = window("Routing", {std::move(ignored)}, "root")
                          .withClick([&]() { ++rootClicks; })
                          .withMouseFilter(MouseFilter::Stop);
    host->setTree(std::move(root));

    UINode *action = host->findById("action");
    REQUIRE(action != nullptr);
    auto tree = host->tree();
    REQUIRE(!tree->nodes.empty());
    CHECK_EQ(tree->nodes[size_t(action->parent)].id, std::string("ignored"));

    UISystem::pendingEvents().clear();
    UISystem::clickQueue().clear();
    UIEvent event;
    event.host = host;
    event.hostName = host->getName();
    event.nodeId = action->id;
    event.nodeIndex = int(action - tree->nodes.data());
    event.kind = "click";
    event.handlerIndex = action->handlerClick;
    UISystem::pendingEvents().push_back(event);
    UISystem::dispatchEvents();

    CHECK_EQ(childClicks, 1);
    CHECK_EQ(ignoredClicks, 0);
    CHECK_EQ(rootClicks, 1);
    CHECK_EQ(UISystem::consumeClickFor(host->getName()), std::string("action"));

    action->mouseFilter = MouseFilter::Stop;
    UISystem::pendingEvents().push_back(event);
    UISystem::dispatchEvents();
    CHECK_EQ(childClicks, 2);
    CHECK_EQ(rootClicks, 1);
}
