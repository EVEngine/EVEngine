#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/CapabilityOwned.h"
#include "common/Diagnostic.h"
#include "common/Status.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

/**
 * Minimal capability interface for these cases. `capabilityName` is the only
 * contract the registry needs; the test never depends on a production interface.
 */
class IWidget {
public:
    static constexpr const char* capabilityName = "test.IWidget";
    virtual ~IWidget()                          = default;
    virtual int value() const                   = 0;
};

class Widget final : public IWidget {
public:
    explicit Widget(int initial) : value_(initial) {}
    int value() const override { return value_; }

private:
    int value_ = 0;
};

using Widgets = eve::cap::OwnedProviderRegistry<IWidget>;

constexpr const char* kWidgetId = "widget.main";

/** Each case starts and ends with the caller-owned and owned tables empty. */
struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

}  // namespace

TEST_CASE("Capability.ownedAbsentIsUnsupported") {
    CapabilityReset reset;

    auto absent = Widgets::acquire(kWidgetId);
    REQUIRE(!absent.ok());
    CHECK_EQ(firstDiagnosticCode(absent.status()), eve::DiagnosticCode::Unsupported);

    // A handle for an id that was never published is unsupported, not stale.
    auto unknown = Widgets::acquire(eve::cap::OwnedProviderHandle{"widget.never", 1});
    CHECK(!unknown.ok());
    CHECK_EQ(firstDiagnosticCode(unknown.status()), eve::DiagnosticCode::Unsupported);

    // Unloading something that was never published is not a silent success.
    CHECK(!Widgets::unload(eve::cap::OwnedProviderHandle{"widget.never", 1}).ok());
    CHECK(!Widgets::acquire(kWidgetId).ok());
}

TEST_CASE("Capability.ownedPublishAcquireAndUnload") {
    CapabilityReset reset;

    auto published = Widgets::provide(kWidgetId, std::make_shared<Widget>(7));
    REQUIRE(published.ok());
    const eve::cap::OwnedProviderHandle handle = published.value();
    CHECK(handle.id == std::string(kWidgetId));
    CHECK(handle.generation != 0);

    auto lease = Widgets::acquire(handle);
    REQUIRE(lease.ok());
    REQUIRE(lease.value().operator bool());
    CHECK_EQ(lease.value()->value(), 7);
    CHECK(lease.value().handle() == handle);

    // Acquiring by id takes the newest published generation.
    auto newest = Widgets::acquire(kWidgetId);
    REQUIRE(newest.ok());
    CHECK_EQ(newest.value()->value(), 7);

    REQUIRE(Widgets::unload(handle).ok());
    auto retired = Widgets::acquire(handle);
    CHECK(!retired.ok());
    CHECK_EQ(firstDiagnosticCode(retired.status()), eve::DiagnosticCode::Conflict);
    CHECK(!Widgets::acquire(kWidgetId).ok());

    // Retiring twice is a stale handle, not a silent success.
    auto again = Widgets::unload(handle);
    CHECK(!again.ok());
    CHECK_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::Conflict);
}

TEST_CASE("Capability.ownedDuplicatePublishIsRejected") {
    CapabilityReset reset;

    auto first = Widgets::provide(kWidgetId, std::make_shared<Widget>(1));
    REQUIRE(first.ok());

    // An owned provider is replaced only after an explicit unload, so a second
    // publish of the same id cannot silently retire the live generation.
    auto duplicate = Widgets::provide(kWidgetId, std::make_shared<Widget>(2));
    CHECK(!duplicate.ok());
    CHECK_EQ(firstDiagnosticCode(duplicate.status()), eve::DiagnosticCode::Conflict);

    auto current = Widgets::acquire(kWidgetId);
    REQUIRE(current.ok());
    CHECK_EQ(current.value()->value(), 1);
}

TEST_CASE("Capability.ownedRepublishAfterUnloadAdvancesGeneration") {
    CapabilityReset reset;

    auto first = Widgets::provide(kWidgetId, std::make_shared<Widget>(1));
    REQUIRE(first.ok());
    REQUIRE(Widgets::unload(first.value()).ok());

    auto second = Widgets::provide(kWidgetId, std::make_shared<Widget>(2));
    REQUIRE(second.ok());
    CHECK(second.value().generation > first.value().generation);

    // The retired handle stays dead instead of aliasing the republished provider.
    auto stale = Widgets::acquire(first.value());
    CHECK(!stale.ok());
    CHECK_EQ(firstDiagnosticCode(stale.status()), eve::DiagnosticCode::Conflict);

    auto current = Widgets::acquire(kWidgetId);
    REQUIRE(current.ok());
    CHECK_EQ(current.value()->value(), 2);

    // Retiring the retired generation must not retire the live one.
    CHECK(!Widgets::unload(first.value()).ok());
    auto survivor = Widgets::acquire(kWidgetId);
    REQUIRE(survivor.ok());
    CHECK_EQ(survivor.value()->value(), 2);
}

TEST_CASE("Capability.ownedLeaseOutlivesRetirement") {
    CapabilityReset reset;

    auto published = Widgets::provide(kWidgetId, std::make_shared<Widget>(11));
    REQUIRE(published.ok());
    auto lease = Widgets::acquire(published.value());
    REQUIRE(lease.ok());

    REQUIRE(Widgets::unload(published.value()).ok());

    // Retirement stops new acquisitions but does not destroy an outstanding lease.
    CHECK(!Widgets::acquire(published.value()).ok());
    CHECK(lease.value().operator bool());
    CHECK_EQ(lease.value()->value(), 11);
}

TEST_CASE("Capability.ownedRejectsNullProvider") {
    CapabilityReset reset;

    std::shared_ptr<IWidget> empty;
    auto                     rejected = Widgets::provide(kWidgetId, empty);
    REQUIRE(!rejected.ok());
    CHECK_EQ(firstDiagnosticCode(rejected.status()), eve::DiagnosticCode::InvalidArgument);
    CHECK(!Widgets::acquire(kWidgetId).ok());
}

TEST_CASE("Capability.ownedPublishMirrorsTheBorrowedSlot") {
    CapabilityReset reset;

    Widget callerOwned{5};
    eve::cap::provide<IWidget>(&callerOwned);
    CHECK(eve::cap::query<IWidget>() == &callerOwned);

    // Publishing an owned provider also exposes it through the borrowed slot, so
    // existing `query` / `ProviderRef` consumers resolve the same instance.
    auto owned = Widgets::provide(kWidgetId, std::make_shared<Widget>(9));
    REQUIRE(owned.ok());
    IWidget* borrowed = eve::cap::query<IWidget>();
    REQUIRE(borrowed != nullptr);
    CHECK_EQ(borrowed->value(), 9);

    // Unloading revokes the borrowed pointer only while it is still the same one.
    REQUIRE(Widgets::unload(owned.value()).ok());
    CHECK(eve::cap::query<IWidget>() == nullptr);
}

TEST_CASE("Capability.providerRefBindsCallerOwnedProvider") {
    CapabilityReset reset;

    // Absent: an empty reference, not a null dereference.
    auto absent = eve::cap::ProviderRef<IWidget>::bind();
    CHECK(!static_cast<bool>(absent));
    CHECK(absent.get() == nullptr);

    Widget widget{42};
    eve::cap::provide<IWidget>(&widget);

    auto present = eve::cap::ProviderRef<IWidget>::bind();
    REQUIRE(static_cast<bool>(present));
    CHECK(present.get() == &widget);
    CHECK_EQ(present->value(), 42);
}

TEST_CASE("Capability.providerRefIsBindOnceAndMoveOnly") {
    // The type is the fence: it cannot be copied, cannot be produced without an
    // explicit bind, and has no refresh, so a per-frame lookup cannot hide in it.
    static_assert(!std::is_copy_constructible_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(!std::is_copy_assignable_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(std::is_move_constructible_v<eve::cap::ProviderRef<IWidget>>);
    // Storage as a member needs an empty state; only `bind()` names a provider.
    static_assert(std::is_default_constructible_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(std::is_default_constructible_v<eve::cap::OwnedProviderLease<IWidget>>);

    CapabilityReset reset;
    Widget          widget{3};
    eve::cap::provide<IWidget>(&widget);

    auto first  = eve::cap::ProviderRef<IWidget>::bind();
    auto second = std::move(first);
    REQUIRE(second.get() == &widget);
    CHECK(first.get() == nullptr);
}

TEST_CASE("Capability.leaseMovedFromIsEmpty") {
    CapabilityReset reset;

    auto published = Widgets::provide(kWidgetId, std::make_shared<Widget>(21));
    REQUIRE(published.ok());

    auto acquired = Widgets::acquire(published.value());
    REQUIRE(acquired.ok());
    eve::cap::OwnedProviderLease<IWidget> lease = std::move(acquired).value();

    auto moved = std::move(lease);
    CHECK(moved.operator bool());
    CHECK_EQ(moved->value(), 21);
    // The moved-from lease must not keep addressing the old generation.
    CHECK(!lease.operator bool());
    CHECK(lease.handle().id.empty());
    CHECK_EQ(lease.handle().generation, std::uint64_t{0});
}
