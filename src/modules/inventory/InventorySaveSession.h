#pragma once
#include "common/Export.h"


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

/** @brief Identity namespace rewritten during a content-version save migration. */
enum class InventorySaveIdKind { Item, EquipmentSlot };

/** @brief Coordinates persistent inventory owners without duplicating their live state. */
class EVENGINE_API_FOUNDATION InventorySaveSession {
public:
    /**
     * @brief Validated owning inventory state awaiting publication.
     * @remarks Move-only typestate. It has no accessors because callers may only commit or discard it.
     */
    class EVENGINE_API_FOUNDATION PreparedRestore {
    public:
        /** @brief Prepared restore. */
        ~PreparedRestore();
        /** @brief Prepared restore. */
        PreparedRestore(PreparedRestore &&) noexcept;
        /** @brief Operator =. */
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
     * @return `eve.inventory.save-session` version 1 protobuf JSON, or a structured failure.
     * @remarks Item definitions and runtime change events are external content/transient state and are excluded.
     *          This compatibility projection shares its schema and validation with snapshotBinary().
     * @thread Owning simulation thread only.
     * @reentrancy No callbacks or inventory hooks are invoked.
     */
    [[nodiscard]] eve::Result<std::string> snapshotJson() const;

    /**
     * @brief Capture versioned protobuf bytes using proto/inventory_save.proto.
     * @return Owning wire bytes, or a structured failure.
     * @thread Owning simulation thread only.
     * @cost O(total inventory state), once per save.
     */
    [[nodiscard]] eve::Result<std::string> snapshotBinary() const;

    /**
     * @brief Validate protobuf bytes and prepare an atomic restore.
     * @param bytes Borrowed wire bytes needed only during this call.
     * @return Move-only state ready for no-fail publication.
     * @remarks Unknown fields and unsupported versions are rejected to avoid silently losing state.
     * @thread Owning simulation thread only.
     * @cost O(total inventory state), once per load.
     */
    [[nodiscard]] eve::Result<PreparedRestore> prepareRestoreSnapshotBinary(std::string_view bytes) const;

    /**
     * @brief Validate and atomically restore a protobuf inventory save.
     * @param bytes Borrowed wire bytes needed only during this call.
     * @return Applied status, or a failure with both participants unchanged.
     * @thread Owning simulation thread only.
     * @cost O(total inventory state), once per load.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshotBinary(std::string_view bytes);

    /** @brief Rewrite one known content identity in an owning Protobuf save without live mutation.
     * @param bytes Source InventorySave wire bytes.
     * @param kind Item definition ID or equipment slot name.
     * @param oldId Identity in the older content version.
     * @param newId Identity in the current content version.
     * @return Rewritten wire bytes or a structured validation failure.
     * @cost Linear in the inventory snapshot; batch migration calls at load boundaries.
     */
    [[nodiscard]] static eve::Result<std::string> rewriteSnapshotBinaryId(std::string_view    bytes,
                                                                          InventorySaveIdKind kind,
                                                                          std::string_view    oldId,
                                                                          std::string_view    newId);

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
