#include "ScriptTest.h"

#include "common/Diagnostic.h"
#include "common/ScriptProvider.h"
#include "common/Status.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

const char* kScriptProviderContent = R"SQ(
class Counter {
    total = 0.0
    function onTick(dt) { total += dt; return total }
    function ping() { total += 1.0 }
    function label(dt) { return "not a number" }
    function boom(dt) { throw "deliberate provider failure" }
    function scaleDamage(vehicleId, defId, amount, zone, sourceId) { return amount * 0.5 }
}
provider <- Counter()
)SQ";

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

/** Capability the seam tests declare; the tests below own its whole lifetime. */
constexpr const char* kSeamCapability = "ScriptProviderSeamTest";

}  // namespace

/**
 * The registry roots a live VM object, so it must be empty before this fixture's VM
 * is destroyed. The destructor body runs before the base class members (including
 * `vm`) are torn down, which is exactly the ordering the registry requires.
 *
 * Declared seams are not published state, so `clearAll` keeps them; this fixture
 * withdraws whatever a case declared, which keeps the cases order-independent when
 * several of them share one process.
 */
class ScriptProviderTest : public ScriptTest {
public:
    ScriptProviderTest() : ScriptTest(kScriptProviderContent) {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
    }

    ~ScriptProviderTest() {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            auto removed = registry.removeCapability(capability);
            removed.ignore("fixture teardown withdraws only the seams this case declared");
        }
    }

private:
    std::vector<std::string> declaredBefore_;
};

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.publishAndCallNumber") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());
    REQUIRE(published.value().name == std::string("test.counter"));
    REQUIRE(published.value().generation != 0);
    REQUIRE_EQ(registry.size(), std::size_t{1});
    REQUIRE(registry.contains("test.counter"));

    auto first = registry.callNumber(published.value(), "onTick", 0.5);
    REQUIRE(first.ok());
    REQUIRE(std::abs(first.value() - 0.5) < 1e-6);

    // The call runs on the live instance, so state persists across invocations.
    auto second = registry.callNumber(published.value(), "onTick", 0.25);
    REQUIRE(second.ok());
    REQUIRE(std::abs(second.value() - 0.75) < 1e-6);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.voidCallAndMethodFailures") {
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    REQUIRE(registry.call(published.value(), "ping").ok());
    auto ticks = registry.callNumber(published.value(), "onTick", 0.0);
    REQUIRE(ticks.ok());
    REQUIRE(std::abs(ticks.value() - 1.0) < 1e-6);

    // A method the provider does not implement is Unsupported, never a silent no-op
    // and never a crash.
    auto missing = registry.call(published.value(), "notAMethod");
    REQUIRE(!missing.ok());
    REQUIRE_EQ(firstDiagnosticCode(missing.status()), eve::DiagnosticCode::Unsupported);

    auto nonNumeric = registry.callNumber(published.value(), "label", 0.0);
    REQUIRE(!nonNumeric.ok());
    REQUIRE_EQ(firstDiagnosticCode(nonNumeric.status()), eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.scriptErrorsAreReportedNotRaised") {
    // Provider methods are user script. A failing one must come back as a structured
    // `Failed` result carrying the script error, never as a raise into the native
    // frame that called it.
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    auto raised = registry.call(published.value(), "boom", 0.0);
    REQUIRE(!raised.ok());
    REQUIRE_EQ(firstDiagnosticCode(raised.status()), eve::DiagnosticCode::Failed);
    REQUIRE(raised.status().describe().find("deliberate provider failure") != std::string::npos);

    // The provider is still usable afterwards: the failed call restored the stack.
    auto recovered = registry.callNumber(published.value(), "onTick", 2.0);
    REQUIRE(recovered.ok());
    REQUIRE(std::abs(recovered.value() - 2.0) < 1e-6);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.retirementIsNeverAliased") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto first = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(first.ok());
    auto second = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(second.ok());
    REQUIRE(second.value().generation > first.value().generation);
    REQUIRE_EQ(registry.generationOf("test.counter"), second.value().generation);

    // Republishing retires the older generation rather than aliasing it.
    auto superseded = registry.call(first.value(), "ping");
    REQUIRE(!superseded.ok());
    REQUIRE_EQ(firstDiagnosticCode(superseded.status()), eve::DiagnosticCode::StaleHandle);

    REQUIRE(registry.unload(second.value()).ok());
    REQUIRE(!registry.contains("test.counter"));
    REQUIRE_EQ(registry.generationOf("test.counter"), std::uint64_t{0});
    REQUIRE_EQ(registry.size(), std::size_t{0});

    auto retired = registry.call(second.value(), "ping");
    REQUIRE(!retired.ok());
    REQUIRE_EQ(firstDiagnosticCode(retired.status()), eve::DiagnosticCode::StaleHandle);

    // Retiring twice is stale, not a silent success.
    auto again = registry.unload(second.value());
    REQUIRE(!again.ok());
    REQUIRE_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::StaleHandle);

    // A later publish must not let either old handle resolve against it.
    auto third = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(third.ok());
    for (const auto& stale : {first.value(), second.value()}) {
        auto refused = registry.call(stale, "ping");
        REQUIRE(!refused.ok());
        REQUIRE_EQ(firstDiagnosticCode(refused.status()), eve::DiagnosticCode::StaleHandle);
    }
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.rejectsBadPublishArguments") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto emptyName = registry.publish("", vm.find("provider"));
    REQUIRE(!emptyName.ok());
    REQUIRE_EQ(firstDiagnosticCode(emptyName.status()), eve::DiagnosticCode::InvalidArgument);

    ssq::Object unbound;
    auto        notAnInstance = registry.publish("test.none", unbound);
    REQUIRE(!notAnInstance.ok());
    REQUIRE_EQ(firstDiagnosticCode(notAnInstance.status()), eve::DiagnosticCode::InvalidArgument);
    REQUIRE_EQ(registry.size(), std::size_t{0});

    // A handle for a name that was never published is stale, not a lookup failure.
    auto unknown = registry.call(eve::ScriptProviderHandle{"test.absent", 7}, "ping");
    REQUIRE(!unknown.ok());
    REQUIRE_EQ(firstDiagnosticCode(unknown.status()), eve::DiagnosticCode::StaleHandle);
}

namespace {

/** Records what the registry bound, so the seam tests can prove the callbacks ran. */
struct SeamRecorder {
    std::vector<std::string> bound;
    std::vector<std::string> unbound;

    eve::Result<void> onBind(const eve::ScriptProviderHandle& handle) {
        bound.push_back(handle.name);
        return eve::Result<void>::success();
    }

    eve::Result<void> onUnbind(const eve::ScriptProviderHandle& handle) {
        unbound.push_back(handle.name);
        return eve::Result<void>::success();
    }
};

}  // namespace

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.capabilitySeamRequiresDeclaration") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    // Without a declared seam the capability is not answerable, and the refusal is
    // visible rather than a published provider that serves nothing.
    REQUIRE(!registry.hasCapabilitySeam(kSeamCapability));
    auto refused = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(!refused.ok());
    REQUIRE_EQ(firstDiagnosticCode(refused.status()), eve::DiagnosticCode::NotFound);
    REQUIRE_EQ(registry.size(), std::size_t{0});

    // The declaration is what makes it answerable, and it is discoverable.
    registry.declareCapability(
        kSeamCapability, [](const eve::ScriptProviderHandle&) { return eve::Result<void>::success(); },
        [](const eve::ScriptProviderHandle&) { return eve::Result<void>::success(); });
    REQUIRE(registry.hasCapabilitySeam(kSeamCapability));
    const auto available = registry.capabilities();
    REQUIRE(std::find(available.begin(), available.end(), std::string(kSeamCapability)) != available.end());

    auto published = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(published.ok());
    REQUIRE_EQ(published.value().name, std::string("test.seam"));

    const auto listed = registry.published();
    REQUIRE_EQ(listed.size(), std::size_t{1});
    REQUIRE_EQ(listed.front().capability, std::string(kSeamCapability));

    // Withdrawing the seam stops further binding but leaves the live generation
    // usable, so a provider can still be retired through the handle it was given.
    REQUIRE(registry.removeCapability(kSeamCapability).ok());
    REQUIRE(!registry.hasCapabilitySeam(kSeamCapability));
    auto afterWithdrawal = registry.publishForCapability(kSeamCapability, "test.seam2", vm.find("provider"));
    REQUIRE(!afterWithdrawal.ok());
    REQUIRE_EQ(firstDiagnosticCode(afterWithdrawal.status()), eve::DiagnosticCode::NotFound);
    REQUIRE(registry.call(published.value(), "ping").ok());
    REQUIRE(registry.unload(published.value()).ok());

    auto again = registry.removeCapability(kSeamCapability);
    REQUIRE(!again.ok());
    REQUIRE_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::NotFound);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.capabilitySeamBindsAndUnbinds") {
    auto& registry = eve::ScriptProviderRegistry::instance();
    auto  recorder = std::make_shared<SeamRecorder>();

    registry.declareCapability(
        kSeamCapability, [recorder](const eve::ScriptProviderHandle& handle) { return recorder->onBind(handle); },
        [recorder](const eve::ScriptProviderHandle& handle) { return recorder->onUnbind(handle); });

    auto published = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(published.ok());
    REQUIRE_EQ(recorder->bound.size(), std::size_t{1});
    REQUIRE_EQ(recorder->bound.front(), std::string("test.seam"));
    REQUIRE(recorder->unbound.empty());

    // Superseding runs the unbinder for the retired generation, then binds the new one.
    auto superseded = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(superseded.ok());
    REQUIRE_EQ(recorder->unbound.size(), std::size_t{1});
    REQUIRE_EQ(recorder->unbound.front(), std::string("test.seam"));
    REQUIRE_EQ(recorder->bound.size(), std::size_t{2});

    REQUIRE(registry.unload(superseded.value()).ok());
    REQUIRE_EQ(recorder->unbound.size(), std::size_t{2});
    REQUIRE_EQ(registry.size(), std::size_t{0});
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.failedBindIsRolledBack") {
    auto& registry = eve::ScriptProviderRegistry::instance();
    int   unbinds  = 0;

    registry.declareCapability(
        kSeamCapability,
        [](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "the consumer was already claimed", handle.name));
        },
        [&unbinds](const eve::ScriptProviderHandle&) -> eve::Result<void> {
            ++unbinds;
            return eve::Result<void>::success();
        });

    auto failed = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(!failed.ok());
    REQUIRE_EQ(firstDiagnosticCode(failed.status()), eve::DiagnosticCode::Conflict);

    // Atomic: no half-bound provider stays observable, and the rollback ran the
    // unbinder so a partially installed adapter cannot survive.
    REQUIRE_EQ(unbinds, 1);
    REQUIRE_EQ(registry.size(), std::size_t{0});
    REQUIRE(!registry.contains("test.seam"));
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.clearAllRunsUnbinders") {
    auto& registry = eve::ScriptProviderRegistry::instance();
    int   unbinds  = 0;

    registry.declareCapability(
        kSeamCapability, [](const eve::ScriptProviderHandle&) { return eve::Result<void>::success(); },
        [&unbinds](const eve::ScriptProviderHandle&) -> eve::Result<void> {
            ++unbinds;
            return eve::Result<void>::success();
        });

    REQUIRE(registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider")).ok());
    registry.clearAll();
    REQUIRE_EQ(unbinds, 1);
    REQUIRE_EQ(registry.size(), std::size_t{0});
    // Declared seams are composition-time declarations, not published state.
    REQUIRE(registry.hasCapabilitySeam(kSeamCapability));
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.callsCarryScalarArguments") {
    // A capability seam needs more than one number: `IVehicleDamageModifier` takes a
    // stable id, a template id, an amount, a zone and a source id.
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    const eve::ScriptArg args[] = {
        eve::ScriptArg::fromText("tank#1"), eve::ScriptArg::fromText("tank"), eve::ScriptArg::fromNumber(12.5),
        eve::ScriptArg::fromText("front"),  eve::ScriptArg::fromInteger(7),
    };
    auto scaled = registry.callNumber(published.value(), "scaleDamage", args);
    REQUIRE(scaled.ok());
    REQUIRE(std::abs(scaled.value() - 6.25) < 1e-6);

    // Wrong arity is a reported failure, and it leaves the provider usable.
    const eve::ScriptArg tooFew[] = {eve::ScriptArg::fromText("tank#1")};
    auto                 arity    = registry.callNumber(published.value(), "scaleDamage", tooFew);
    REQUIRE(!arity.ok());
    REQUIRE_EQ(firstDiagnosticCode(arity.status()), eve::DiagnosticCode::Failed);
    REQUIRE(registry.call(published.value(), "ping").ok());
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.callSetupErrorsStayReported") {
    // A parameter-count mistake is raised by `SQVM::StartCall`, and `SQVM::Execute`
    // sends that straight to the VM's error handler *regardless* of `sq_call`'s
    // `raiseerror`. This case is the regression guard for that path: with the engine
    // hook installed (see ScriptTest) it is a reported failure, not a raise into the
    // native frame and not a crash inside simplesquirrel's default handler.
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    auto tooMany = registry.call(published.value(), "ping", 1.0);
    REQUIRE(!tooMany.ok());
    REQUIRE_EQ(firstDiagnosticCode(tooMany.status()), eve::DiagnosticCode::Failed);
    REQUIRE(tooMany.status().describe().find("parameters") != std::string::npos);

    auto tooFew = registry.call(published.value(), "onTick");
    REQUIRE(!tooFew.ok());
    REQUIRE_EQ(firstDiagnosticCode(tooFew.status()), eve::DiagnosticCode::Failed);

    // Both failures left the VM stack intact: the provider still answers.
    auto recovered = registry.callNumber(published.value(), "onTick", 3.0);
    REQUIRE(recovered.ok());
    REQUIRE(std::abs(recovered.value() - 3.0) < 1e-6);
}
