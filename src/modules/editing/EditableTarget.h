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
