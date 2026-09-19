#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/Status.h"

#include <memory>
#include <type_traits>

namespace {

/**
 * Minimal capability interface for these cases. `capabilityName` is the only
 * contract the registry needs; the test never depends on a production interface.
 */
class IWidget {
public:
    static constexpr const char* capabilityName = "test.IWidget";
    virtual ~IWidget() = default;
    virtual int value() const = 0;
};

class Widget final : public IWidget {
public:
    explicit Widget(int initial) : value_(initial) {}
    int value() const override { return value_; }

private:
    int value_ = 0;
};

/** Each case starts and ends with both capability tables empty. */
struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

}  // namespace

TEST_CASE("Capability.ownedAbsentIsNotFound") {
    CapabilityReset reset;

    auto absent = eve::cap::ProviderLease<IWidget>::acquire();
    CHECK(!absent.ok());
    CHECK_EQ(firstDiagnosticCode(absent.status()), eve::DiagnosticCode::NotFound);

    auto generation = eve::cap::detail::ownedGenerationRaw(IWidget::capabilityName);
    CHECK_EQ(generation, std::uint64_t{0});
}

TEST_CASE("Capability.ownedPublishAcquireAndUnload") {
    CapabilityReset reset;

    auto published = eve::cap::provideOwned<IWidget>(std::make_shared<Widget>(7));
    REQUIRE(published.ok());
    const eve::cap::ProviderHandle handle = published.value();
    CHECK(handle.capability == std::string(IWidget::capabilityName));
    CHECK(handle.generation != 0);

    auto lease = eve::cap::ProviderLease<IWidget>::acquire(handle);
    REQUIRE(lease.ok());
    CHECK(lease.value().operator bool());
    CHECK_EQ(lease.value()->value(), 7);
    CHECK(lease.value().handle() == handle);

    // The newest published generation is the one the unqualified acquire returns.
    auto newest = eve::cap::ProviderLease<IWidget>::acquire();
    REQUIRE(newest.ok());
    CHECK_EQ(newest.value()->value(), 7);

    REQUIRE(eve::cap::unloadOwned(handle).ok());
    CHECK(!eve::cap::ProviderLease<IWidget>::acquire(handle).ok());
    CHECK(!eve::cap::ProviderLease<IWidget>::acquire().ok());
    CHECK_EQ(firstDiagnosticCode(eve::cap::ProviderLease<IWidget>::acquire(handle).status()),
             eve::DiagnosticCode::StaleHandle);

    // Retiring twice is a stale handle, not a silent success.
    auto again = eve::cap::unloadOwned(handle);
    CHECK(!again.ok());
    CHECK_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::StaleHandle);
}

TEST_CASE("Capability.ownedSupersededHandleIsStale") {
    CapabilityReset reset;

    auto first = eve::cap::provideOwned<IWidget>(std::make_shared<Widget>(1));
    REQUIRE(first.ok());
    auto second = eve::cap::provideOwned<IWidget>(std::make_shared<Widget>(2));
    REQUIRE(second.ok());

    // A newer generation supersedes the older handle rather than aliasing it.
    CHECK(second.value().generation > first.value().generation);
    auto stale = eve::cap::ProviderLease<IWidget>::acquire(first.value());
    CHECK(!stale.ok());
    CHECK_EQ(firstDiagnosticCode(stale.status()), eve::DiagnosticCode::StaleHandle);

    auto current = eve::cap::ProviderLease<IWidget>::acquire();
    REQUIRE(current.ok());
    CHECK_EQ(current.value()->value(), 2);

    // Retiring the superseded generation must not retire the live one.
    CHECK(!eve::cap::unloadOwned(first.value()).ok());
    auto survivor = eve::cap::ProviderLease<IWidget>::acquire();
    REQUIRE(survivor.ok());
    CHECK_EQ(survivor.value()->value(), 2);
}

TEST_CASE("Capability.ownedLeaseOutlivesRetirement") {
    CapabilityReset reset;

    auto published = eve::cap::provideOwned<IWidget>(std::make_shared<Widget>(11));
    REQUIRE(published.ok());
    auto lease = eve::cap::ProviderLease<IWidget>::acquire(published.value());
    REQUIRE(lease.ok());

    REQUIRE(eve::cap::unloadOwned(published.value()).ok());

    // Retirement stops new acquisitions but does not destroy an outstanding lease.
    CHECK(!eve::cap::ProviderLease<IWidget>::acquire(published.value()).ok());
    CHECK(lease.value().operator bool());
    CHECK_EQ(lease.value()->value(), 11);
}

TEST_CASE("Capability.ownedRejectsNullProvider") {
    CapabilityReset reset;

    std::shared_ptr<IWidget> empty;
    auto rejected = eve::cap::provideOwned<IWidget>(empty);
    CHECK(!rejected.ok());
    CHECK_EQ(firstDiagnosticCode(rejected.status()), eve::DiagnosticCode::InvalidArgument);
    CHECK_EQ(eve::cap::detail::ownedGenerationRaw(IWidget::capabilityName), std::uint64_t{0});
}

TEST_CASE("Capability.ownedAndCallerOwnedModesAreIndependent") {
    CapabilityReset reset;

    Widget callerOwned{5};
    eve::cap::provide<IWidget>(&callerOwned);
    CHECK(eve::cap::query<IWidget>() == &callerOwned);

    // Publishing into the owned table must not disturb the caller-owned slot, and
    // the caller-owned path must still resolve its own provider.
    auto owned = eve::cap::provideOwned<IWidget>(std::make_shared<Widget>(9));
    REQUIRE(owned.ok());
    CHECK(eve::cap::query<IWidget>() == &callerOwned);

    auto lease = eve::cap::ProviderLease<IWidget>::acquire();
    REQUIRE(lease.ok());
    CHECK_EQ(lease.value()->value(), 9);
}

TEST_CASE("Capability.providerRefResolvesCallerOwnedProvider") {
    CapabilityReset reset;

    // Absent: an empty reference, not a null dereference.
    auto absent = eve::cap::ProviderRef<IWidget>::resolve();
    CHECK(!static_cast<bool>(absent));
    CHECK(absent.get() == nullptr);

    Widget widget{42};
    eve::cap::provide<IWidget>(&widget);

    auto present = eve::cap::ProviderRef<IWidget>::resolve();
    REQUIRE(static_cast<bool>(present));
    CHECK(present.get() == &widget);
    CHECK_EQ(present->value(), 42);
}

TEST_CASE("Capability.providerRefIsResolveOnceAndMoveOnly") {
    // The type is the fence: it cannot be copied, cannot be produced without an
    // explicit resolve, and has no refresh, so a per-frame lookup cannot hide in it.
    static_assert(!std::is_copy_constructible_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(!std::is_copy_assignable_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(std::is_move_constructible_v<eve::cap::ProviderRef<IWidget>>);
    // Storage as a member needs an empty state; only `resolve()` names a provider.
    static_assert(std::is_default_constructible_v<eve::cap::ProviderLease<IWidget>>);

    CapabilityReset reset;
    Widget widget{3};
    eve::cap::provide<IWidget>(&widget);

    auto first  = eve::cap::ProviderRef<IWidget>::resolve();
    auto second = std::move(first);
    CHECK(second.get() == &widget);
    CHECK(first.get() == nullptr);
}

TEST_CASE("Capability.leaseMovedFromIsEmpty") {
    CapabilityReset reset;

    auto published = eve::cap::provideOwned<IWidget>(std::make_shared<Widget>(21));
    REQUIRE(published.ok());

    auto acquired = eve::cap::ProviderLease<IWidget>::acquire(published.value());
    REQUIRE(acquired.ok());
    eve::cap::ProviderLease<IWidget> lease = std::move(acquired).value();

    auto moved = std::move(lease);
    CHECK(moved.operator bool());
    CHECK_EQ(moved->value(), 21);
    // The moved-from lease must not keep addressing the old generation.
    CHECK(!lease.operator bool());
    CHECK(lease.handle().capability.empty());
    CHECK_EQ(lease.handle().generation, std::uint64_t{0});
}
