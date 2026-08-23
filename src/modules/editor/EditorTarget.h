#pragma once

#include <string>
#include <type_traits>

namespace eve::editor {

/** @brief Integer rectangle describing data invalidated by an edit. */
struct EditRegion {
    int minX = 0;
    int minY = 0;
    int maxX = -1;
    int maxY = -1;

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
    void include(const EditRegion &other) {
        if (other.empty()) return;
        include(other.minX, other.minY);
        include(other.maxX, other.maxY);
    }
};

/** @brief Base protocol for data that editor tools may modify. */
class IEditableTarget {
public:
    virtual ~IEditableTarget() = default;
    virtual const std::string &targetId() const = 0;
    virtual unsigned long long revision() const = 0;
    virtual EditRegion dirtyRegion() const = 0;
    virtual void clearDirtyRegion() = 0;

    template <typename Capability>
    Capability *query() {
        static_assert(std::is_polymorphic_v<Capability>, "target capability must be polymorphic");
        return dynamic_cast<Capability *>(this);
    }
    template <typename Capability>
    const Capability *query() const {
        static_assert(std::is_polymorphic_v<Capability>, "target capability must be polymorphic");
        return dynamic_cast<const Capability *>(this);
    }
};

/** @brief Capability for a bounded two-dimensional editable field. */
class IGridTarget {
public:
    virtual ~IGridTarget() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual bool inBounds(int x, int y) const = 0;
};

/** @brief Capability for integer fields such as tiles, zones and masks. */
class IIntFieldTarget : public virtual IGridTarget {
public:
    virtual int readInt(int x, int y) const = 0;
    virtual bool writeInt(int x, int y, int value) = 0;
};

/** @brief Capability for scalar fields such as terrain height and density. */
class IScalarFieldTarget : public virtual IGridTarget {
public:
    virtual float readScalar(int x, int y) const = 0;
    virtual bool writeScalar(int x, int y, float value) = 0;
    virtual float sampleScalar(float x, float y) const = 0;
};

}  // namespace eve::editor
