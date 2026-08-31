#pragma once

/**
 * @file InventorySaveSession.h
 * @brief Versioned atomic persistence boundary for one bag and equipment set.
 */

#include "common/Result.h"

#include <memory>
#include <string>
#include <string_view>

namespace eve::inventory {

class Bag;
class EquipmentSet;

/** @brief Coordinates persistent inventory owners without duplicating their live state. */
class InventorySaveSession {
public:
    /**
     * @brief Validated owning inventory state awaiting publication.
     * @remarks Move-only typestate. It has no accessors because callers may only commit or discard it.
     */
    class PreparedRestore {
    public:
        ~PreparedRestore();
        PreparedRestore(PreparedRestore &&) noexcept;
        PreparedRestore &operator=(PreparedRestore &&) noexcept;
        PreparedRestore(const PreparedRestore &) = delete;
        PreparedRestore &operator=(const PreparedRestore &) = delete;

    private:
        friend class InventorySaveSession;

        PreparedRestore();
        std::unique_ptr<Bag> bag_;
        std::unique_ptr<EquipmentSet> equipment_;
        int largestInstanceId = 0;
    };

    InventorySaveSession() = default;

    /**
     * @brief Bind the authoritative inventory participants.
     * @param bag Borrowed bag owner.
     * @param equipment Borrowed equipment owner.
     * @remarks Both objects must outlive this session. Use on their owning simulation thread.
     */
    void bind(Bag &bag, EquipmentSet &equipment) noexcept;

    /**
     * @brief Capture the complete bag and equipment state.
     * @return Deterministic `eve.inventory.save-session` version 1 JSON, or a structured failure.
     * @remarks Item definitions and runtime change events are external content/transient state and are excluded.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks or inventory hooks are invoked.
     */
    [[nodiscard]] eve::Result<std::string> snapshotJson() const;

    /**
     * @brief Parse and validate a snapshot without mutating either participant.
     * @param json UTF-8 JSON produced by snapshotJson().
     * @return Move-only prepared state, or a structured failure.
     * @remarks Every referenced item definition and named policy must already be registered.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks or inventory hooks are invoked.
     */
    [[nodiscard]] eve::Result<PreparedRestore> prepareRestoreSnapshotJson(std::string_view json) const;

    /**
     * @brief Publish a previously validated state without failure or callbacks.
     * @param prepared Owning prepared state returned by this codec contract.
     * @pre The value has not previously been moved from or committed.
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks or inventory hooks are invoked.
     */
    void commitPrepared(PreparedRestore prepared) noexcept;

    /**
     * @brief Validate and atomically restore both bound inventory participants.
     * @param json UTF-8 JSON produced by snapshotJson().
     * @return Applied status, or a structured failure with both participants unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(std::string_view json);

private:
    Bag *bag_ = nullptr;
    EquipmentSet *equipment_ = nullptr;
};

}  // namespace eve::inventory
