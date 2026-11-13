#include "common/Capability.h"
#include <exception>
#include <limits>
#include <utility>
#include "common/Assert.h"
#include "common/BootWarmup.h"
#include "common/ListenerStaging.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::cap::detail {

namespace {

struct Listener {
    void* impl = nullptr;
    int priority = 0;
};

struct Slot {
    void* service = nullptr;
    std::vector<Listener> listeners;  // kept sorted by priority
};

/**
 * Keyed by capability name rather than by type so a provider compiled into a
 * plugin resolves to the same slot as the host. Function-local so the table is
 * constructed before any static module registration touches it.
 */
struct CapabilityHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
};
using CapabilitySlots = std::unordered_map<std::string, Slot, CapabilityHash, std::equal_to<>>;
// @lifetime Borrow of the process-owned table, cleared before its member storage is destroyed.
CapabilitySlots* activeSlots        = nullptr;
bool             slotsDestroyed     = false;
std::uint64_t    registryGeneration = 1;
struct SlotOwner {
    CapabilitySlots table;
    SlotOwner() { activeSlots = &table; }
    SlotOwner(const SlotOwner&)            = delete;
    SlotOwner& operator=(const SlotOwner&) = delete;
    ~SlotOwner() {
        activeSlots    = nullptr;
        slotsDestroyed = true;
    }
};
CapabilitySlots& slots() {
    if (slotsDestroyed) {
        EV_ASSERT(false, "Capability publication after process registry shutdown");
        std::terminate();
    }
    static SlotOwner owner;
    return owner.table;
}

}  // namespace

}  // namespace eve::cap::detail

namespace eve::cap {
struct ListenerStaging::Impl {
    struct Family {
        std::string                   name;
        std::vector<detail::Listener> pending;
    };
    std::vector<Family>       families;
    std::uint64_t             generation = 0;
    bool                      entered = false, paused = false, published = false;
    Impl*                     previous = nullptr;
    static thread_local Impl* current;
    static thread_local Impl* liveHead;
    Family*                   find(std::string_view name) noexcept {
        for (auto& family : families)
            if (family.name == name) return &family;
        return nullptr;
    }
};
thread_local ListenerStaging::Impl* ListenerStaging::Impl::current  = nullptr;
thread_local ListenerStaging::Impl* ListenerStaging::Impl::liveHead = nullptr;
ListenerStaging::ListenerStaging(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ListenerStaging::~ListenerStaging() {
    if (!impl_->entered) return;
    if (impl_->paused || Impl::current != impl_.get() || Impl::liveHead != impl_.get()) {
        EV_ASSERT(false, "Listener staging must outlive pauses and close in lexical order");
        std::terminate();
    }
    Impl::current  = nullptr;
    Impl::liveHead = impl_->previous;
}
ListenerStagingPause::ListenerStagingPause(ListenerStaging& owner) noexcept : owner_(&owner) {}
ListenerStagingPause::ListenerStagingPause(ListenerStagingPause&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)) {}
ListenerStagingPause::~ListenerStagingPause() {
    if (!owner_) return;
    if (ListenerStaging::Impl::current || ListenerStaging::Impl::liveHead != owner_->impl_.get()) {
        EV_ASSERT(false, "External callback left a listener staging scope active");
        std::terminate();
    }
    owner_->impl_->paused          = false;
    ListenerStaging::Impl::current = owner_->impl_.get();
}
Result<std::unique_ptr<ListenerStaging>> ListenerStaging::begin(std::span<const std::string_view> families) {
    using R = Result<std::unique_ptr<ListenerStaging>>;
    if (Impl::current || detail::slotsDestroyed)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                            "Listener capture already active or registry retired"));
    if (families.empty() || families.size() > 64)
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected 1..64 listener families"));
    auto impl = std::make_unique<Impl>();
    for (auto name : families) {
        if (name.empty() || name.size() > 256 || name.find('\0') != std::string_view::npos || impl->find(name))
            return R::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid or duplicate listener family"));
        impl->families.push_back({std::string(name), {}});
    }
    impl->generation       = detail::registryGeneration;
    auto owner             = std::unique_ptr<ListenerStaging>(new ListenerStaging(std::move(impl)));
    owner->impl_->previous = Impl::liveHead;
    Impl::liveHead         = owner->impl_.get();
    Impl::current          = owner->impl_.get();
    owner->impl_->entered  = true;
    return R::success(std::move(owner));
}
Result<ListenerStagingPause> ListenerStaging::pause() {
    if (Impl::current != impl_.get() || impl_->paused || impl_->published)
        return Result<ListenerStagingPause>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Listener capture is not active"));
    impl_->paused = true;
    Impl::current = nullptr;
    return Result<ListenerStagingPause>::success(ListenerStagingPause(*this));
}
}  // namespace eve::cap

namespace eve::cap::detail {
namespace {
void insertListener(std::vector<Listener>& list, void* impl, int priority) {
    if (std::any_of(list.begin(), list.end(), [impl](const auto& entry) { return entry.impl == impl; })) return;
    const auto where = std::upper_bound(list.begin(), list.end(), priority,
                                        [](int value, const auto& entry) { return value < entry.priority; });
    list.insert(where, {impl, priority});
}
}  // namespace
struct ListenerStageAccess {
    enum class Route { Captured, PassThrough };
    static Route capture(const char* name, void* provider, int priority) {
        auto* current = ListenerStaging::Impl::current;
        if (!current || current->published) return Route::PassThrough;
        auto* family = current->find(name);
        if (!family) return Route::PassThrough;
        if (activeSlots) {
            auto slot = activeSlots->find(std::string_view(name));
            if (slot != activeSlots->end() &&
                std::any_of(slot->second.listeners.begin(), slot->second.listeners.end(),
                            [provider](const auto& entry) { return entry.impl == provider; }))
                return Route::Captured;
        }
        insertListener(family->pending, provider, priority);
        return Route::Captured;
    }
    static void withdraw(const char* name, void* provider) {
        for (auto* scope = ListenerStaging::Impl::liveHead; scope; scope = scope->previous)
            if (auto* family = scope->find(name))
                std::erase_if(family->pending, [provider](const auto& entry) { return entry.impl == provider; });
    }
};
}  // namespace eve::cap::detail

namespace eve::cap {
Result<void> ListenerStaging::publish() {
    if (Impl::current != impl_.get() || impl_->paused || impl_->published)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Listener capture is inactive or published"));
    if (impl_->generation != detail::registryGeneration)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Capability registry reset during listener capture"));
    auto& live      = detail::slots();
    auto  projected = live;
    for (const auto& family : impl_->families)
        for (const auto& listener : family.pending)
            detail::insertListener(projected[family.name].listeners, listener.impl, listener.priority);
    static_assert(noexcept(live.swap(projected)), "Listener publication must be a no-throw exchange");
    live.swap(projected);
    impl_->published = true;
    for (auto& family : impl_->families) family.pending.clear();
    return Result<void>::success();
}
}  // namespace eve::cap

namespace eve::cap::detail {

void provideRaw(const char* name, void* impl) {
    if (!name) return;
    if (!impl) {
        if (!activeSlots) return;
        const auto found = activeSlots->find(std::string_view(name));
        if (found != activeSlots->end()) found->second.service = nullptr;
        return;
    }
    slots()[name].service = impl;
}

void* queryRaw(const char* name) {
    if (!name) return nullptr;
    auto* table = activeSlots;
    if (!table) return nullptr;
    auto it = table->find(std::string_view(name));
    return it == table->end() ? nullptr : it->second.service;
}

void revokeRaw(const char* name, void* impl) {
    if (!name) return;
    auto* table = activeSlots;
    if (!table) return;
    auto it = table->find(std::string_view(name));
    // Only clear when `impl` is still the current provider, so a stale revoke
    // from a torn-down module cannot unregister its replacement.
    if (it != table->end() && it->second.service == impl) it->second.service = nullptr;
}

void addListenerRaw(const char* name, void* impl, int priority) {
    if (!name || !impl) return;
    if (ListenerStageAccess::capture(name, impl, priority) == ListenerStageAccess::Route::Captured) return;
    insertListener(slots()[name].listeners, impl, priority);
}

void removeListenerRaw(const char* name, void* impl) {
    if (!name) return;
    ListenerStageAccess::withdraw(name, impl);
    auto* table = activeSlots;
    if (!table) return;
    auto it = table->find(std::string_view(name));
    if (it == table->end()) return;
    auto& list = it->second.listeners;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [impl](const Listener& l) { return l.impl == impl; }),
               list.end());
}

size_t listenerCountRaw(const char* name) {
    if (!name) return 0;
    auto* table = activeSlots;
    if (!table) return 0;
    auto it = table->find(std::string_view(name));
    return it == table->end() ? 0 : it->second.listeners.size();
}

void* listenerAtRaw(const char* name, size_t index) {
    if (!name) return nullptr;
    auto* table = activeSlots;
    if (!table) return nullptr;
    auto it = table->find(std::string_view(name));
    if (it == table->end() || index >= it->second.listeners.size()) return nullptr;
    return it->second.listeners[index].impl;
}

void clearOwnedRaw();  // CapabilityOwned.cpp

void clearAllRaw() {
    if (registryGeneration == std::numeric_limits<std::uint64_t>::max()) std::terminate();
    ++registryGeneration;
    slots().clear();
    clearOwnedRaw();
}

}  // namespace eve::cap::detail

namespace eve::boot {
namespace {

void (*gVulkanWarmup)() = nullptr;

}  // namespace

void registerVulkanInstanceWarmup(void (*start)()) { gVulkanWarmup = start; }

void startVulkanInstanceWarmup() {
    if (gVulkanWarmup) gVulkanWarmup();
}

}  // namespace eve::boot
