#include "ScriptTest.h"

#include "common/Diagnostic.h"
#include "common/ScriptProvider.h"
#include "common/Status.h"

#include <cmath>

namespace {

const char* kScriptProviderContent = R"SQ(
class Counter {
    total = 0.0
    function onTick(dt) { total += dt; return total }
    function ping() { total += 1.0 }
    function label(dt) { return "not a number" }
    function boom(dt) { throw "deliberate provider failure" }
}
provider <- Counter()
)SQ";

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

}  // namespace

/**
 * The registry roots a live VM object, so it must be empty before this fixture's VM
 * is destroyed. The destructor body runs before the base class members (including
 * `vm`) are torn down, which is exactly the ordering the registry requires.
 */
class ScriptProviderTest : public ScriptTest {
public:
    ScriptProviderTest() : ScriptTest(kScriptProviderContent) { eve::ScriptProviderRegistry::instance().clearAll(); }
    ~ScriptProviderTest() { eve::ScriptProviderRegistry::instance().clearAll(); }
};

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.publishAndCallNumber") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());
    CHECK(published.value().name == std::string("test.counter"));
    CHECK(published.value().generation != 0);
    CHECK_EQ(registry.size(), std::size_t{1});
    CHECK(registry.contains("test.counter"));

    auto first = registry.callNumber(published.value(), "onTick", 0.5);
    REQUIRE(first.ok());
    CHECK(std::abs(first.value() - 0.5) < 1e-6);

    // The call runs on the live instance, so state persists across invocations.
    auto second = registry.callNumber(published.value(), "onTick", 0.25);
    REQUIRE(second.ok());
    CHECK(std::abs(second.value() - 0.75) < 1e-6);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.voidCallAndMethodFailures") {
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    REQUIRE(registry.call(published.value(), "ping").ok());
    auto ticks = registry.callNumber(published.value(), "onTick", 0.0);
    REQUIRE(ticks.ok());
    CHECK(std::abs(ticks.value() - 1.0) < 1e-6);

    // A method the provider does not implement is Unsupported, never a silent no-op
    // and never a crash.
    auto missing = registry.call(published.value(), "notAMethod");
    CHECK(!missing.ok());
    CHECK_EQ(firstDiagnosticCode(missing.status()), eve::DiagnosticCode::Unsupported);

    auto nonNumeric = registry.callNumber(published.value(), "label", 0.0);
    CHECK(!nonNumeric.ok());
    CHECK_EQ(firstDiagnosticCode(nonNumeric.status()), eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.scriptErrorsAreReportedNotRaised") {
    // Provider methods are user script. A failing one must come back as a structured
    // `Failed` result carrying the script error, never as a raise into the native
    // frame that called it.
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    auto raised = registry.call(published.value(), "boom", 0.0);
    CHECK(!raised.ok());
    CHECK_EQ(firstDiagnosticCode(raised.status()), eve::DiagnosticCode::Failed);
    CHECK(raised.status().describe().find("deliberate provider failure") != std::string::npos);

    // The provider is still usable afterwards: the failed call restored the stack.
    auto recovered = registry.callNumber(published.value(), "onTick", 2.0);
    REQUIRE(recovered.ok());
    CHECK(std::abs(recovered.value() - 2.0) < 1e-6);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.retirementIsNeverAliased") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto first = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(first.ok());
    auto second = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(second.ok());
    CHECK(second.value().generation > first.value().generation);
    CHECK_EQ(registry.generationOf("test.counter"), second.value().generation);

    // Republishing retires the older generation rather than aliasing it.
    auto superseded = registry.call(first.value(), "ping");
    CHECK(!superseded.ok());
    CHECK_EQ(firstDiagnosticCode(superseded.status()), eve::DiagnosticCode::StaleHandle);

    REQUIRE(registry.unload(second.value()).ok());
    CHECK(!registry.contains("test.counter"));
    CHECK_EQ(registry.generationOf("test.counter"), std::uint64_t{0});
    CHECK_EQ(registry.size(), std::size_t{0});

    auto retired = registry.call(second.value(), "ping");
    CHECK(!retired.ok());
    CHECK_EQ(firstDiagnosticCode(retired.status()), eve::DiagnosticCode::StaleHandle);

    // Retiring twice is stale, not a silent success.
    auto again = registry.unload(second.value());
    CHECK(!again.ok());
    CHECK_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::StaleHandle);

    // A later publish must not let either old handle resolve against it.
    auto third = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(third.ok());
    for (const auto& stale : {first.value(), second.value()}) {
        auto refused = registry.call(stale, "ping");
        CHECK(!refused.ok());
        CHECK_EQ(firstDiagnosticCode(refused.status()), eve::DiagnosticCode::StaleHandle);
    }
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.rejectsBadPublishArguments") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto emptyName = registry.publish("", vm.find("provider"));
    CHECK(!emptyName.ok());
    CHECK_EQ(firstDiagnosticCode(emptyName.status()), eve::DiagnosticCode::InvalidArgument);

    ssq::Object unbound;
    auto        notAnInstance = registry.publish("test.none", unbound);
    CHECK(!notAnInstance.ok());
    CHECK_EQ(firstDiagnosticCode(notAnInstance.status()), eve::DiagnosticCode::InvalidArgument);
    CHECK_EQ(registry.size(), std::size_t{0});

    // A handle for a name that was never published is stale, not a lookup failure.
    auto unknown = registry.call(eve::ScriptProviderHandle{"test.absent", 7}, "ping");
    CHECK(!unknown.ok());
    CHECK_EQ(firstDiagnosticCode(unknown.status()), eve::DiagnosticCode::StaleHandle);
}
