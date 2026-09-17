#include "physics/PhysicsHandles.h"
#include "common/Exception.h"
#include "common/Identity.h"

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

eve::PersistentId makePhysicsWorldPersistentId(PhysicsWorldHandle runtimeHandle) {
    eve::PersistentId::Bytes bytes{};
    const std::uint64_t packed = runtimeHandle.packed();
    static std::atomic<std::uint64_t> sequence{1u};
    const std::uint64_t serial = sequence.fetch_add(1u, std::memory_order_relaxed);
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::uint8_t>((packed >> (i * 8u)) & 0xffu);
        bytes[8 + i] = static_cast<std::uint8_t>((serial >> (i * 8u)) & 0xffu);
    }
    bytes[6] = static_cast<std::uint8_t>(0x40u | (bytes[6] & 0x0fu));
    bytes[8] = static_cast<std::uint8_t>(0x80u | (bytes[8] & 0x3fu));
    return eve::PersistentId(bytes);
}

}  // namespace eve::physics::detail
