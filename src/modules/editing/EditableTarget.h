#pragma once
#include <string>
#include <type_traits>
#include <vector>
#include "common/BorrowedRef.h"
#include "editing/EditingProtocol.h"
namespace eve::editing {
/** @brief EditRegion public API. */
struct EditRegion {
    int  minX = 0, minY = 0, maxX = -1, maxY = -1;
    /** @brief Empty. */
    bool empty() const { return maxX < minX || maxY < minY; }
    /** @brief Clears clear. */
    void clear() { *this = {}; }
    /** @brief Include. */
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
    /** @brief Include. */
    void include(const EditRegion& o) {
        if (!o.empty()) {
            /** @brief Include. */
            include(o.minX, o.minY);
            /** @brief Include. */
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

/** @brief IEditableTarget public API. */
class IEditableTarget {
public:
    /** @brief Releases IEditableTarget resources. */
    virtual ~IEditableTarget()                     = default;
    /** @brief Target id. */
    virtual TargetId           targetId() const    = 0;
    /** @brief Revision. */
    virtual std::uint64_t      revision() const    = 0;
    /** @brief Dirty region. */
    virtual EditRegion         dirtyRegion() const = 0;
    /** @brief Clears dirty region. */
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
    /** @brief Capability. */
    eve::OptionalRef<C> capability() {
        /** @brief Constexpr. */
        if constexpr (requires { C::editingCapabilityId(); }) {
            if (void* raw = queryCapability(C::editingCapabilityId()))
                return eve::OptionalRef<C>(std::ref(*static_cast<C*>(raw)));
        }
        /** @brief Constexpr. */
        if constexpr (std::is_polymorphic_v<C>) {
            if (auto* typed = dynamic_cast<C*>(this)) return eve::OptionalRef<C>(std::ref(*typed));
        }
        return {};
    }
    template <class C>
    /** @brief Capability. */
    eve::OptionalRef<const C> capability() const {
        /** @brief Constexpr. */
        if constexpr (std::is_polymorphic_v<C>) {
            if (auto* typed = dynamic_cast<const C*>(this)) return eve::OptionalRef<const C>(std::ref(*typed));
        }
        return {};
    }
    template <class C>
    /** @brief Queries query. */
    C* query() {
        auto cap = capability<C>();
        return cap ? &cap->get() : nullptr;
    }
    template <class C>
    /** @brief Queries query. */
    const C* query() const {
        auto cap = capability<C>();
        return cap ? &cap->get() : nullptr;
    }
};
/**
 * @brief Dirty region and revision shared by every editable document target.
 *
 * Inherit this instead of hand-writing the dirty-region accessors, the revision
 * accessor and their two members in each target. Identity (targetId) and the
 * descriptor's type and capability list stay per-target; only the state is
 * common. Mutation goes through the helpers below rather than through the
 * members, so a target cannot advance its revision without recording what
 * changed. The initial revision is a constructor argument because targets
 * legitimately differ: some start at the first revision, some at "not yet
 * revised".
 *
 * @ownership Non-owning shared state: a derived target owns this subobject and
 *            the state owns only its dirty region and revision values.
 * @lifetime The subobject lives exactly as long as the derived target; the
 *           accessors return by value, so no reference escapes.
 * @thread Editing/protocol thread of the owning target; no synchronization is
 *         provided here.
 * @reentrancy Side-effect free accessors; the protected mutators touch only
 *             this subobject and never invoke callbacks.
 * @remarks Revision here is editing::Revision, the protocol's plain 64-bit
 *          revision, not the eve::Revision strong type used by property
 *          providers. Keeping the protocol boundary on the plain integer is
 *          deliberate: ExpectedRevision/BaseRevision are plain integers too.
 */
#if defined(_MSC_VER)
// A target that inherits this base together with another editing interface that
// also derives from IEditableTarget (IDomainOperationTarget, for example) receives
// the three members below through dominance instead of declaring them itself, and
// MSVC reports C4250 for each such member. That resolution is well-defined and is
// the intent: these overrides are the final overriders, so every call through
// IEditableTarget dispatches here. MSVC documents C4250 as informational, and it is
// disabled at the single class that creates the pattern because MSVC reports it at
// each derived class -- which lives in a header that includes this one, after this
// point, so the suppression below covers them.
#pragma warning(disable : 4250)
#endif
/** @brief EditableTargetState public API. */
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
    void markDirty(int x, int y) {
        /** @brief Widen dirty. */
        widenDirty(x, y);
        /** @brief Bump revision. */
        bumpRevision();
    }
    /** @brief Include a whole region and advance the revision. */
    void markDirty(const EditRegion& region) {
        /** @brief Widen dirty. */
        widenDirty(region);
        /** @brief Bump revision. */
        bumpRevision();
    }
    /** @brief Mark an unknown area dirty and advance the revision. */
    void markDirty() {
        /** @brief Widen dirty. */
        widenDirty();
        /** @brief Bump revision. */
        bumpRevision();
    }

private:
    EditRegion dirty_;
    Revision   revision_;
};
/** @brief FieldWriteStatus public API. */
enum class FieldWriteStatus { Applied, Unchanged, Rejected };
/** @brief IGridTarget public API. */
class IGridTarget {
public:
    /** @brief Releases IGridTarget resources. */
    virtual ~IGridTarget()                    = default;
    /** @brief Width. */
    virtual int  width() const                = 0;
    /** @brief Height. */
    virtual int  height() const               = 0;
    /** @brief True if cell. */
    virtual bool containsCell(int, int) const = 0;
};
/** @brief IIntFieldTarget public API. */
class IIntFieldTarget : public virtual IGridTarget {
public:
    /** @brief Editing capability id. */
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editing.target.int-field.v1"); }
    /** @brief Reads int. */
    virtual int  readInt(int, int) const = 0;
    /** @brief Writes int. */
    [[nodiscard]] virtual FieldWriteStatus writeInt(int, int, int) = 0;
};
/** @brief IScalarFieldTarget public API. */
class IScalarFieldTarget : public virtual IGridTarget {
public:
    /** @brief Editing capability id. */
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editing.target.scalar-field.v1"); }
    /** @brief Reads scalar. */
    virtual float            readScalar(int, int) const       = 0;
    /** @brief Writes scalar. */
    [[nodiscard]] virtual FieldWriteStatus writeScalar(int, int, float)     = 0;
    /** @brief Sample scalar. */
    virtual float            sampleScalar(float, float) const = 0;
};
/** @brief Optional capability exposing a deterministic, persistence-safe editing snapshot. */
class IEditingSnapshotProvider {
public:
    /** @brief Releases IEditingSnapshotProvider resources. */
    virtual ~IEditingSnapshotProvider() = default;
    /** @brief Stable capability identity shared by every editable domain target. */
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editing.target.snapshot.v1"); }
    /** @brief Capture the target without exposing mutable runtime storage. */
    [[nodiscard]] virtual Value snapshotValue() const = 0;
};
}  // namespace eve::editing
