#pragma once
#include <string>
#include <type_traits>
#include <vector>
#include "common/BorrowedRef.h"
#include "editing/EditingProtocol.h"
namespace eve::editing {
struct EditRegion {
    int  minX = 0, minY = 0, maxX = -1, maxY = -1;
    bool empty() const { return maxX < minX || maxY < minY; }
    void clear() { *this = {}; }
    void include(int x, int y) {
        if (empty()) {
            minX = maxX = x;
            minY = maxY = y;
            return;
        }
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    void include(const EditRegion& o) {
        if (!o.empty()) {
            include(o.minX, o.minY);
            include(o.maxX, o.maxY);
        }
    }
};

/** @brief Stable discovery metadata exposed by every editable target. */
struct TargetDescriptor {
    TargetId                  id;
    std::string               type;
    Revision                  revision = 0;
    bool                      readOnly = false;
    std::vector<CapabilityId> capabilities;
};

class IEditableTarget {
public:
    virtual ~IEditableTarget()                     = default;
    virtual TargetId           targetId() const    = 0;
    virtual std::uint64_t      revision() const    = 0;
    virtual EditRegion         dirtyRegion() const = 0;
    virtual void               clearDirtyRegion()  = 0;
    /** @brief Describe the target for tools and automation. */
    virtual TargetDescriptor describe() const {
        return {targetId(), {}, revision(), false, {}};
    }
    /**
     * @brief Query an optional stable capability.
     * @param capability Stable capability identifier.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or the capability is explicitly invalidated.
     */
    virtual void* queryCapability(const CapabilityId& capability) {
        (void)capability;
        return nullptr;
    }
    /**
     * @brief Query a capability through its interface-owned stable identity.
     * @tparam C Capability interface declaring static editingCapabilityId().
     * @return Borrowed typed capability, or empty when unsupported.
     * @lifetime Valid until this target is destroyed or the capability is explicitly invalidated.
     */
    template <class C>
    eve::OptionalRef<C> capability() {
        if constexpr (requires { C::editingCapabilityId(); }) {
            if (void* raw = queryCapability(C::editingCapabilityId()))
                return eve::OptionalRef<C>(std::ref(*static_cast<C*>(raw)));
        }
        if constexpr (std::is_polymorphic_v<C>) {
            if (auto* typed = dynamic_cast<C*>(this)) return eve::OptionalRef<C>(std::ref(*typed));
        }
        return {};
    }
    template <class C>
    eve::OptionalRef<const C> capability() const {
        if constexpr (std::is_polymorphic_v<C>) {
            if (auto* typed = dynamic_cast<const C*>(this)) return eve::OptionalRef<const C>(std::ref(*typed));
        }
        return {};
    }
    template <class C>
    C* query() {
        auto cap = capability<C>();
        return cap ? &cap->get() : nullptr;
    }
    template <class C>
    const C* query() const {
        auto cap = capability<C>();
        return cap ? &cap->get() : nullptr;
    }
};
/**
 * @brief Dirty region and revision shared by every editable document target.
 *
 * Inherit this instead of hand-writing dirtyRegion(), clearDirtyRegion(),
 * revision() and their two members in each target. Identity (targetId) and the
 * descriptor's type and capability list stay per-target; only the state is
 * common. Mutation goes through the helpers below rather than through the
 * members, so a target cannot advance its revision without recording what
 * changed. The initial revision is a constructor argument because targets
 * legitimately differ: some start at the first revision, some at "not yet
 * revised".
 *
 * @remarks Revision here is editing::Revision, the protocol's plain 64-bit
 *          revision, not the eve::Revision strong type used by property
 *          providers. Keeping the protocol boundary on the plain integer is
 *          deliberate: ExpectedRevision/BaseRevision are plain integers too.
 */
class EditableTargetState : public virtual IEditableTarget {
public:
    /** @brief Constructs the shared state with an explicit initial revision. */
    explicit EditableTargetState(Revision initial = 1) : revision_(initial) {}

    /** @brief Return the accumulated dirty region. */
    [[nodiscard]] EditRegion dirtyRegion() const override { return dirty_; }

    /** @brief Discard the dirty region; the revision does not change. */
    void clearDirtyRegion() override { dirty_ = {}; }

    /** @brief Return the current revision. */
    [[nodiscard]] std::uint64_t revision() const override { return revision_; }

    /** @brief Return the revision as the shared protocol revision type. */
    [[nodiscard]] Revision revisionValue() const noexcept { return revision_; }

protected:
    /** @brief Widen the dirty region without advancing the revision. */
    void widenDirty(int x, int y) { dirty_.include(x, y); }
    /** @brief Widen the dirty region without advancing the revision. */
    void widenDirty(const EditRegion& region) { dirty_.include(region); }
    /** @brief Mark an unknown area dirty without advancing the revision. */
    void widenDirty() { dirty_.include(0, 0); }
    /** @brief Advance the revision without touching the dirty region. */
    void bumpRevision() { ++revision_; }
    /** @brief Adopt a revision observed elsewhere, without advancing it. */
    void setRevision(Revision revision) { revision_ = revision; }
    /** @brief Replace the dirty region wholesale, without advancing the revision. */
    void setDirtyRegion(const EditRegion& region) { dirty_ = region; }
    /** @brief Include one cell in the dirty region and advance the revision. */
    void markDirty(int x, int y) { widenDirty(x, y); bumpRevision(); }
    /** @brief Include a whole region and advance the revision. */
    void markDirty(const EditRegion& region) { widenDirty(region); bumpRevision(); }
    /** @brief Mark an unknown area dirty and advance the revision. */
    void markDirty() { widenDirty(); bumpRevision(); }

private:
    EditRegion dirty_;
    Revision   revision_;
};
enum class FieldWriteStatus { Applied, Unchanged, Rejected };
class IGridTarget {
public:
    virtual ~IGridTarget()                    = default;
    virtual int  width() const                = 0;
    virtual int  height() const               = 0;
    virtual bool containsCell(int, int) const = 0;
};
class IIntFieldTarget : public virtual IGridTarget {
public:
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editing.target.int-field.v1"); }
    virtual int  readInt(int, int) const = 0;
    [[nodiscard]] virtual FieldWriteStatus writeInt(int, int, int) = 0;
};
class IScalarFieldTarget : public virtual IGridTarget {
public:
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editing.target.scalar-field.v1"); }
    virtual float            readScalar(int, int) const       = 0;
    [[nodiscard]] virtual FieldWriteStatus writeScalar(int, int, float)     = 0;
    virtual float            sampleScalar(float, float) const = 0;
};
/** @brief Optional capability exposing a deterministic, persistence-safe editing snapshot. */
class IEditingSnapshotProvider {
public:
    virtual ~IEditingSnapshotProvider() = default;
    /** @brief Stable capability identity shared by every editable domain target. */
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editing.target.snapshot.v1"); }
    /** @brief Capture the target without exposing mutable runtime storage. */
    [[nodiscard]] virtual Value snapshotValue() const = 0;
};
}  // namespace eve::editing
