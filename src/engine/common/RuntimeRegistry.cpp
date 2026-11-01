/**
 * @file RuntimeRegistry.cpp
 * @brief Out-of-line core of the tag-free runtime registry slots.
 *
 * The slot bookkeeping - free-list reuse, generation bumping, retirement, owner-epoch
 * staleness, pin counting, deferred destruction and release rollback - is identical for
 * every registry, so it is emitted once here instead of once per (T, Tag) pair.
 */

#include "common/RuntimeRegistry.h"

#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::script::detail {

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
    void* object = slot.object;
    if (object == nullptr) return;
    // Clear the slot before running the destructor. A payload may itself hold pins into
    // this store (resource dependencies do exactly that), and every pin release re-enters
    // unpin(); observing the object as already gone keeps that re-entry from resolving or
    // destroying a payload whose destructor is still on the stack.
    slot.object = nullptr;
    destroy_(object);
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

Result<void*> RuntimeSlotStore::pin(std::uint32_t index, std::uint32_t generation, std::uint64_t ownerEpoch) {
    if (!coordinatesValid(index, generation)) {
        return Result<void*>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "cannot pin an invalid runtime handle", {}, {}, "runtime.registry"));
    }
    if (ownerEpoch != ownerEpoch_) return registryStale<void*>();
    const auto slotIndex = findSlot(index, generation, ownerEpoch);
    if (!slotIndex) return registryStale<void*>();
    Slot& slot = slots_[*slotIndex];
    if (slot.object == nullptr || slot.generation != generation || slot.orphaned) return registryStale<void*>();
    if (slot.pins == std::numeric_limits<std::uint32_t>::max())
        return registryFailure<void*>(DiagnosticCode::InvariantViolation, "runtime registry pin count exhausted");
    ++slot.pins;
    return Result<void*>::success(slot.object);
}

void RuntimeSlotStore::unpin(std::uint32_t index, std::uint64_t ownerEpoch) noexcept {
    if (ownerEpoch != ownerEpoch_ || index >= slots_.size()) return;
    Slot& slot = slots_[index];
    if (slot.pins == 0) return;
    --slot.pins;
    if (slot.pins != 0 || !slot.orphaned) return;
    destroySlot(slot);
    slot.orphaned = false;
    if (!slot.retired) {
        try {
            freeSlots_.push_back(index);
        } catch (...) {
            // The object is already destroyed. Losing the free-list entry only
            // wastes one slot index; it can never alias a live object.
        }
    }
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
    if (slot.pins != 0) {
        // Outstanding pins keep the object alive, so the slot must not be handed
        // out again; the handle itself is stale as soon as the generation moves.
        slot.orphaned = true;
        if (next)
            slot.generation = *next;
        else
            slot.retired = true;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
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
        Slot&      slot = slots_[index];
        const auto next = nextGeneration(slot.generation);
        if (slot.pins != 0) {
            // Objects kept alive by a pin survive the clear; their slots stay out
            // of the free list until unpin() destroys them.
            slot.orphaned = true;
            if (next)
                slot.generation = *next;
            else
                slot.retired = true;
            continue;
        }
        destroySlot(slot);
        if (next) {
            slot.generation = *next;
            freeSlots_.push_back(index);
        } else {
            slot.retired = true;
        }
    }
}

}  // namespace eve::script::detail
