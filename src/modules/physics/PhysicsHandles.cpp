#include "physics/PhysicsHandles.h"
#include "common/Exception.h"

#include <atomic>
#include <limits>

namespace eve::physics::detail {

PhysicsWorldHandle allocatePhysicsWorldHandle() {
    using Handle = PhysicsWorldHandle;
    using Index  = Handle::index_type;

    static std::atomic<Index> nextIndex{1u};
    Index                     current = nextIndex.load(std::memory_order_relaxed);
    for (;;) {
        if (current == Handle::invalidIndex)
            throw eve::Exception("Physics: process-local world handle space exhausted");
        const Index next = static_cast<Index>(current + 1u);
        if (nextIndex.compare_exchange_weak(current, next, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return Handle(current, 1u);
        }
    }
}

}  // namespace eve::physics::detail
