#pragma once
#include <string>
#include <type_traits>
#include <vector>
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
class IEditableTarget {
public:
    virtual ~IEditableTarget()                     = default;
    virtual const std::string& targetId() const    = 0;
    virtual unsigned long long revision() const    = 0;
    virtual EditRegion         dirtyRegion() const = 0;
    virtual void               clearDirtyRegion()  = 0;
    template <class C>
    C* query() {
        static_assert(std::is_polymorphic_v<C>);
        return dynamic_cast<C*>(this);
    }
    template <class C>
    const C* query() const {
        static_assert(std::is_polymorphic_v<C>);
        return dynamic_cast<const C*>(this);
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
    virtual int  readInt(int, int) const = 0;
    virtual bool writeInt(int, int, int) = 0;
};
class IScalarFieldTarget : public virtual IGridTarget {
public:
    virtual float            readScalar(int, int) const       = 0;
    virtual FieldWriteStatus writeScalar(int, int, float)     = 0;
    virtual float            sampleScalar(float, float) const = 0;
};
struct TargetDescriptor {
    TargetId                  id;
    std::string               type;
    Revision                  revision = 0;
    bool                      readOnly = false;
    std::vector<CapabilityId> capabilities;
};
class IEditableTargetV2 : public virtual IEditableTarget {
public:
    ~IEditableTargetV2() override                                 = default;
    virtual TargetDescriptor describe() const                     = 0;
    virtual void*            queryCapability(const CapabilityId&) = 0;
};
}  // namespace eve::editing
