#pragma once

#include "statepatch/StatePatch.h"

#include <utility>

namespace eve::test_support {

/**
 * @brief Test-only lease for a Store-owned StatePatch batch.
 *
 * The lease keeps the handle and the synchronous borrowed view together. It
 * checks the allocation result before resolving and explicitly observes the
 * release result during destruction, so tests do not accidentally model a
 * raw-pointer ownership contract.
 */
struct StatePatchBatchLease {
    StatePatchBatchLease() = default;

    StatePatchBatchLease(eve::statepatch::Store* owner, eve::statepatch::PatchBatchHandleRef reference,
                         eve::script::Borrowed<eve::statepatch::PatchBatch> view) noexcept
        : owner(owner), reference(reference), view(view) {}

    StatePatchBatchLease(const StatePatchBatchLease&)            = delete;
    StatePatchBatchLease& operator=(const StatePatchBatchLease&) = delete;

    StatePatchBatchLease(StatePatchBatchLease&& other) noexcept
        : owner(other.owner), reference(other.reference), view(other.view) {
        other.owner     = nullptr;
        other.reference = {};
        other.view      = {};
    }

    StatePatchBatchLease& operator=(StatePatchBatchLease&& other) noexcept {
        if (this == &other) return *this;
        release();
        owner           = other.owner;
        reference       = other.reference;
        view            = other.view;
        other.owner     = nullptr;
        other.reference = {};
        other.view      = {};
        return *this;
    }

    ~StatePatchBatchLease() { release(); }

    /** @brief Explicitly releases the Store-owned batch, if still live. */
    void release() noexcept {
        if (owner != nullptr && reference.isValid())
            owner->releaseBatch(reference).ignore("release test StatePatch batch");
        owner     = nullptr;
        reference = {};
        view      = {};
    }

    eve::statepatch::Store*                            owner = nullptr;
    eve::statepatch::PatchBatchHandleRef               reference;
    eve::script::Borrowed<eve::statepatch::PatchBatch> view;
};

/**
 * @brief Allocates and resolves one StatePatch batch for a synchronous test.
 * @return A checked handle/view lease, or an empty lease on allocation or
 *         resolution failure.
 */
inline StatePatchBatchLease openStatePatchBatch(eve::statepatch::Store& store) {
    auto created = store.newBatch();
    if (!created.ok()) return {};

    const auto reference = std::move(created).takeValue();
    auto       view      = store.resolveBatch(reference);
    if (!view.isBound()) {
        store.releaseBatch(reference).ignore("release unresolved test StatePatch batch");
        return {};
    }
    return StatePatchBatchLease(&store, reference, view);
}

}  // namespace eve::test_support
