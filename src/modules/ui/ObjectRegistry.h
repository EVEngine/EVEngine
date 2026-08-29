#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/RuntimeHandle.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eve::ui {

/** @brief Owner tag for UI database object handles. */
struct UiObjectHandleTag {};

/**
 * @brief Generation-qualified identity for one live UI database object.
 *
 * This is a process-local registry handle, not a persistent object ID. The
 * handle becomes stale when the object is unregistered, including when its
 * slot is subsequently reused for another object.
 */
using ObjectHandle = eve::RuntimeHandle<UiObjectHandleTag>;

/** @brief One registered live script instance. */
struct EVENGINE_API ObjectEntry {
    /** @brief Generation-qualified handle for the live registry slot. */
    ObjectHandle handle = ObjectHandle::invalid();
    /** @brief Reflected script class used to group this entry. */
    std::string className;
    /** @brief Optional display label shown by the database panel. */
    std::string label;
    /** @brief Rooted script instance owned by the registry while live. */
    ssq::Object object;
};

/**
 * @brief Reflectable script-object registry backing the database panel.
 *
 * Holds rooted references to script class instances grouped by class name.
 * The database panel lists entries per class, creates instances through the
 * active Runtime and edits their reflected properties; scripts register their
 * own live objects through `ui.dbRegister(obj)`.
 *
 * All methods are main/UI-thread-affine. The registry owns the rooted script
 * object while its slot is live. Callbacks must retain ObjectHandle values and
 * resolve them again at invocation time; pointers and copied entries are
 * observations and do not extend an entry's lifetime.
 */
class EVENGINE_API ObjectRegistry {
public:
    ObjectRegistry(const ObjectRegistry&) = delete;
    ObjectRegistry& operator=(const ObjectRegistry&) = delete;

    /** @brief Returns the process-local singleton owned by the UI module. */
    static ObjectRegistry& instance();

    /**
     * @brief Creates a class instance through the active Runtime and registers it.
     * @param className Reflected script class name.
     * @return A live ObjectHandle, or a structured failure when the class is
     *         unknown, the Runtime is unavailable, or construction fails.
     */
    [[nodiscard("retain ObjectHandle or explicitly inspect the failure")]]
    eve::Result<ObjectHandle> create(const std::string& className);
    /**
     * @brief Registers an existing live script instance.
     * @param className Class name used for grouping (auto-derived when empty).
     * @param object    Live script instance (rooted while registered).
     * @param label     Optional display label (defaults to "Class #n").
     * @return A live ObjectHandle, or a structured failure for an invalid
     *         object, unavailable Runtime, or allocation failure.
     */
    [[nodiscard("retain ObjectHandle or explicitly inspect the failure")]]
    eve::Result<ObjectHandle> registerObject(const std::string& className, const ssq::Object& object,
                                             const std::string& label = {});
    /**
     * @brief Removes a live entry and advances its slot generation.
     * @param handle Handle returned by create() or registerObject().
     * @return Applied on success; StaleHandle, NotFound, or InvalidArgument
     *         when the handle cannot identify the live entry.
     */
    [[nodiscard]] eve::Result<void> unregister(ObjectHandle handle);
    /** @brief Removes every entry of a class. */
    void clear(const std::string& className);
    /** @brief Removes every entry. */
    void clearAll();

    /** @brief Class names with at least one entry, sorted. */
    std::vector<std::string> classNames() const;
    /** @brief Entries of a class, in registration order. */
    std::vector<ObjectEntry> entries(const std::string& className) const;
    /**
     * @brief Looks up a live entry without transferring ownership.
     * @param handle Candidate generation-qualified handle.
     * @return Borrowed nullable entry for a live handle.
     * @ownership ObjectRegistry owns the rooted entry; callers must not delete or mutate the result.
     * @lifetime Valid only until unregister(), clear(), or another registry mutation.
     * @thread Call on the UI thread owning this registry.
     * @reentrancy The lookup invokes no callbacks and is invalid across registry mutation.
     */
    [[nodiscard("inspect the borrowed entry or explicitly ignore it")]]
    const ObjectEntry* entry(ObjectHandle handle) const noexcept;
    /**
     * @brief Reports whether a valid handle is stale in this registry.
     * @param handle Candidate handle; invalid() is not classified as stale.
     */
    [[nodiscard]] bool isStale(ObjectHandle handle) const noexcept;
    /** @brief Number of entries of a class. */
    size_t count(const std::string& className) const;

private:
    struct Slot {
        std::uint32_t              generation = 1;
        bool                       retired    = false;
        std::optional<ObjectEntry> entry;
    };

    /**
     * @brief Resolves an internal slot for one registry operation.
     * @return Borrowed nullable slot owned by this registry.
     * @ownership ObjectRegistry owns slots; this private helper never transfers ownership.
     * @lifetime Valid only for the duration of the operation and until registry mutation.
     * @thread Call on the UI thread owning this registry.
     * @reentrancy Does not invoke callbacks and is invalid across mutation.
     */
    [[nodiscard]] Slot* slot(ObjectHandle handle) noexcept;
    /**
     * @brief Resolves an internal slot for a read-only operation.
     * @return Borrowed nullable slot owned by this registry.
     * @ownership ObjectRegistry owns slots; this private helper never transfers ownership.
     * @lifetime Valid only for the duration of the operation and until registry mutation.
     * @thread Call on the UI thread owning this registry.
     * @reentrancy Does not invoke callbacks and is invalid across mutation.
     */
    [[nodiscard]] const Slot* slot(ObjectHandle handle) const noexcept;

    ObjectRegistry() = default;
    std::vector<Slot>                                slots_;
    std::vector<std::uint32_t>                       freeSlots_;
    std::map<std::string, std::vector<ObjectHandle>> byClass_;
};

}  // namespace eve::ui
