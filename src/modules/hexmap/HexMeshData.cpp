#include "hexmap/HexMeshData.h"

#include <cmath>

namespace eve::hexmap {

void HexMeshData::clear() noexcept {
    positions_.clear();
    normals_.clear();
    uvs_.clear();
    indices_.clear();
}

std::uint32_t HexMeshData::addVertex(HexVec3 position, float u, float v) {
    const auto index = static_cast<std::uint32_t>(positions_.size() / 3u);
    positions_.push_back(position.x);
    positions_.push_back(position.y);
    positions_.push_back(position.z);
    uvs_.push_back(u);
    uvs_.push_back(v);
    return index;
}

void HexMeshData::addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    indices_.push_back(a);
    indices_.push_back(b);
    indices_.push_back(c);
}

void HexMeshData::addQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    addTriangle(a, c, b);
    addTriangle(b, c, d);
}

void HexMeshData::finalize() noexcept {
    normals_.assign(positions_.size(), 0.f);
    for (std::size_t t = 0; t + 2 < indices_.size(); t += 3) {
        const std::uint32_t ia = indices_[t];
        const std::uint32_t ib = indices_[t + 1];
        const std::uint32_t ic = indices_[t + 2];
        const auto          at = [this](std::uint32_t i) {
            const std::size_t base = static_cast<std::size_t>(i) * 3u;
            return HexVec3{positions_[base], positions_[base + 1], positions_[base + 2]};
        };
        const HexVec3 a  = at(ia);
        const HexVec3 b  = at(ib);
        const HexVec3 c  = at(ic);
        const HexVec3 ab = b - a;
        const HexVec3 ac = c - a;
        HexVec3       n{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
        const float   len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-8f) {
            n.x /= len;
            n.y /= len;
            n.z /= len;
        } else {
            n = HexVec3{0.f, 1.f, 0.f};
        }
        for (std::uint32_t i : {ia, ib, ic}) {
            const std::size_t base = static_cast<std::size_t>(i) * 3u;
            normals_[base]         = n.x;
            normals_[base + 1]     = n.y;
            normals_[base + 2]     = n.z;
        }
    }
}

}  // namespace eve::hexmap
