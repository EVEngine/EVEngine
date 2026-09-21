/**
 * @file SquirrelOwnership.cpp
 * @brief Out-of-line core of the owned-Squirrel-instance factory.
 *
 * makeOwnedSquirrelInstance<T> is instantiated once per wrapper type (66 call
 * sites across 28 files), and its body was ~45 lines of stack discipline,
 * instance creation, typing, rooting and exception handling that has nothing to
 * do with T. Emitting it here means one copy instead of one per wrapper.
 */

#include "common/SquirrelOwnership.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <exception>
#include <string>

namespace eve::script::detail {

Diagnostic ownedInstanceArgumentDiagnostic() {
    return Diagnostic::error(DiagnosticCode::InvalidArgument,
                             "owned Squirrel instance requires a VM and non-null object", {}, {}, "squirrel.ownership");
}

Result<ssq::Object> makeOwnedSquirrelInstanceRaw(HSQUIRRELVM vm, void* object, SquirrelReleaseHook releaseHook,
                                                 OwnedInstanceDestroy destroy, std::size_t typeHash) {
    const SQInteger top         = sq_gettop(vm);
    bool            transferred = false;
    try {
        const HSQOBJECT& classObject = ssq::detail::getClassObj(vm, typeHash);
        sq_pushobject(vm, classObject);
        if (SQ_FAILED(sq_createinstance(vm, -1)))
            throw ssq::RuntimeException("failed to create owned Squirrel instance");
        sq_remove(vm, -2);
        if (SQ_FAILED(sq_setinstanceup(vm, -1, static_cast<SQUserPointer>(object))))
            throw ssq::RuntimeException("failed to attach owned Squirrel instance");
        sq_settypetag(vm, -1, reinterpret_cast<SQUserPointer>(typeHash));
        sq_setreleasehook(vm, -1, releaseHook);
        // From here the live instance owns the object: its release hook is
        // installed, so a later failure must not destroy it here.
        transferred = true;

        ssq::Object result(vm);
        if (SQ_FAILED(sq_getstackobj(vm, -1, &result.getRaw())))
            throw ssq::RuntimeException("failed to root owned Squirrel instance");
        sq_addref(vm, &result.getRaw());
        sq_settop(vm, top);
        return Result<ssq::Object>::success(std::move(result));
    } catch (const std::exception& error) {
        sq_settop(vm, top);
        if (!transferred) {
            destroy(object);
            return Result<ssq::Object>::failure(Diagnostic::error(
                DiagnosticCode::Failed, std::string("owned Squirrel instance creation failed: ") + error.what(), {}, {},
                "squirrel.ownership"));
        }
        return Result<ssq::Object>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("owned Squirrel instance rooting failed: ") + error.what(), {}, {},
            "squirrel.ownership"));
    } catch (...) {
        sq_settop(vm, top);
        if (!transferred) destroy(object);
        return Result<ssq::Object>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "owned Squirrel instance creation failed", {}, {}, "squirrel.ownership"));
    }
}

namespace {

/** @brief Registry failure carrying the shared runtime-registry diagnostic source. */
template <class R>
[[nodiscard]] Result<R> registryFailure(DiagnosticCode code, std::string message) {
    return Result<R>::failure(Diagnostic::error(code, std::move(message), {}, {}, "runtime.registry"));
}

/** @brief Stale-handle failure for the runtime registry. */
template <class R>
[[nodiscard]] Result<R> registryStale() {
    return registryFailure<R>(DiagnosticCode::StaleHandle, "runtime handle is stale or belongs to another owner epoch");
}

}  // namespace

RuntimeSlotStore::~RuntimeSlotStore() {
    for (auto& slot : slots_) destroySlot(slot);
}

bool RuntimeSlotStore::coordinatesValid(std::uint32_t index, std::uint32_t generation) noexcept {
    return index != std::numeric_limits<std::uint32_t>::max() && generation != std::uint32_t{0};
}

std::optional<std::uint32_t> RuntimeSlotStore::nextGeneration(std::uint32_t current) noexcept {
    if (current == std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(current + 1u);
}

std::uint64_t RuntimeSlotStore::nextEpoch() noexcept {
    std::uint64_t value = nextEpoch_.fetch_add(1, std::memory_order_relaxed);
    if (value == 0) value = nextEpoch_.fetch_add(1, std::memory_order_relaxed);
    return value;
}

std::optional<std::uint32_t> RuntimeSlotStore::findSlot(std::uint32_t index, std::uint32_t generation,
                                                        std::uint64_t ownerEpoch) const noexcept {
    if (ownerEpoch != ownerEpoch_ || !coordinatesValid(index, generation) || index >= slots_.size())
        return std::nullopt;
    return index;
}

void RuntimeSlotStore::destroySlot(Slot& slot) noexcept {
    if (slot.object == nullptr) return;
    destroy_(slot.object);
    slot.object = nullptr;
}

Result<RuntimeSlotCoordinates> RuntimeSlotStore::emplace(void* object) {
    try {
        std::uint32_t index    = 0;
        bool          appended = false;
        if (!freeSlots_.empty()) {
            index = freeSlots_.back();
        } else {
            if (slots_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                return registryFailure<RuntimeSlotCoordinates>(DiagnosticCode::Failed,
                                                               "runtime registry exhausted its slot index space");
            }
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
            appended = true;
        }

        Slot& slot = slots_[index];
        if (slot.retired || slot.object != nullptr) {
            if (appended) slots_.pop_back();
            return registryFailure<RuntimeSlotCoordinates>(DiagnosticCode::InvariantViolation,
                                                           "runtime registry selected an occupied slot");
        }
        const RuntimeSlotCoordinates coordinates{index, slot.generation};
        slot.object = object;
        if (!freeSlots_.empty() && freeSlots_.back() == index) freeSlots_.pop_back();
        return Result<RuntimeSlotCoordinates>::success(coordinates);
    } catch (const std::exception& error) {
        return registryFailure<RuntimeSlotCoordinates>(
            DiagnosticCode::Failed, std::string("runtime registry allocation failed: ") + error.what());
    } catch (...) {
        return registryFailure<RuntimeSlotCoordinates>(DiagnosticCode::Failed, "runtime registry allocation failed");
    }
}

void* RuntimeSlotStore::resolve(std::uint32_t index, std::uint32_t generation,
                                std::uint64_t ownerEpoch) const noexcept {
    const auto slotIndex = findSlot(index, generation, ownerEpoch);
    if (!slotIndex) return nullptr;
    const Slot& slot = slots_[*slotIndex];
    return (slot.object != nullptr && slot.generation == generation) ? slot.object : nullptr;
}

Result<void> RuntimeSlotStore::erase(std::uint32_t index, std::uint32_t generation, std::uint64_t ownerEpoch) {
    if (!coordinatesValid(index, generation)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "cannot erase an invalid runtime handle", {}, {}, "runtime.registry"));
    }
    if (ownerEpoch != ownerEpoch_) return registryStale<void>();
    const auto slotIndex = findSlot(index, generation, ownerEpoch);
    if (!slotIndex) return registryStale<void>();
    Slot& slot = slots_[*slotIndex];
    if (slot.object == nullptr || slot.generation != generation) return registryStale<void>();

    const auto next = nextGeneration(slot.generation);
    if (next) {
        try {
            freeSlots_.push_back(index);
        } catch (const std::exception& error) {
            return registryFailure<void>(DiagnosticCode::Failed,
                                         std::string("runtime registry release bookkeeping failed: ") + error.what());
        } catch (...) {
            return registryFailure<void>(DiagnosticCode::Failed, "runtime registry release bookkeeping failed");
        }
    }
    destroySlot(slot);
    if (next)
        slot.generation = *next;
    else
        slot.retired = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool RuntimeSlotStore::isStale(std::uint32_t index, std::uint32_t generation, std::uint64_t ownerEpoch) const noexcept {
    if (!coordinatesValid(index, generation)) return false;
    if (ownerEpoch != ownerEpoch_) return true;
    const auto slotIndex = findSlot(index, generation, ownerEpoch);
    if (!slotIndex) return true;
    const Slot& slot = slots_[*slotIndex];
    return slot.object == nullptr || slot.generation != generation;
}

void RuntimeSlotStore::clear() {
    // Reserve before mutating live slots so free-list bookkeeping cannot fail
    // after ownership has already been released.
    freeSlots_.reserve(slots_.size());
    freeSlots_.clear();
    for (std::uint32_t index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        destroySlot(slot);
        const auto next = nextGeneration(slot.generation);
        if (next) {
            slot.generation = *next;
            freeSlots_.push_back(index);
        } else {
            slot.retired = true;
        }
    }
}

}  // namespace eve::script::detail
