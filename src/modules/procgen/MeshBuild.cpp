#include "procgen/MeshBuild.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {

void MeshBuild::clear() {
    positions_.clear();
    normals_.clear();
    uvs_.clear();
    indices_.clear();
    triangleGroups_.clear();
    groupNames_.clear();
    activeGroup_ = -1;
    meta_.clear();
}

void MeshBuild::reserve(int vertexCount, int indexCount) {
    if (vertexCount > 0) {
        positions_.reserve(size_t(vertexCount) * 3u);
        normals_.reserve(size_t(vertexCount) * 3u);
        uvs_.reserve(size_t(vertexCount) * 2u);
    }
    if (indexCount > 0) {
        indices_.reserve(size_t(indexCount));
        triangleGroups_.reserve(size_t(indexCount) / 3u);
    }
}

void MeshBuild::addVertex(float px, float py, float pz, float nx, float ny, float nz, float u,
                          float v) {
    positions_.push_back(px);
    positions_.push_back(py);
    positions_.push_back(pz);
    normals_.push_back(nx);
    normals_.push_back(ny);
    normals_.push_back(nz);
    uvs_.push_back(u);
    uvs_.push_back(v);
}

void MeshBuild::addTriangle(uint32_t i0, uint32_t i1, uint32_t i2) {
    indices_.push_back(i0);
    indices_.push_back(i1);
    indices_.push_back(i2);
    triangleGroups_.push_back(activeGroup_);
}

int MeshBuild::setActiveGroup(const std::string &name) {
    if (name.empty()) { activeGroup_ = -1; return -1; }
    for (int i = 0; i < int(groupNames_.size()); ++i) {
        if (groupNames_[size_t(i)] == name) { activeGroup_ = i; return i; }
    }
    groupNames_.push_back(name);
    activeGroup_ = int(groupNames_.size()) - 1;
    return activeGroup_;
}

int MeshBuild::getGroupCount() const { return int(groupNames_.size()); }

std::string MeshBuild::getGroupName(int groupIndex) const {
    if (groupIndex < 0 || groupIndex >= getGroupCount()) return {};
    return groupNames_[size_t(groupIndex)];
}

int MeshBuild::getTriangleGroup(int triangleIndex) const {
    if (triangleIndex < 0 || triangleIndex >= int(triangleGroups_.size())) return -1;
    return triangleGroups_[size_t(triangleIndex)];
}

eve::Result<void> MeshBuild::restoreGroupData(std::vector<std::string> names, std::vector<int> assignments,
                                              int activeGroup) {
    const auto invalid = [&](std::string message, std::string path) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ProcgenGroupDataInvalid, std::move(message), std::move(path), {}, "procgen.mesh"));
    };
    if (assignments.size() != indices_.size() / 3u || activeGroup < -1 ||
        activeGroup >= static_cast<int>(names.size()) ||
        std::any_of(names.begin(), names.end(), [](const std::string &name) { return name.empty(); }) ||
        std::any_of(assignments.begin(), assignments.end(),
                    [&](int group) { return group < -1 || group >= static_cast<int>(names.size()); }))
        return invalid("mesh group sidecar is inconsistent with dense mesh streams", "groups");
    groupNames_     = std::move(names);
    triangleGroups_ = std::move(assignments);
    activeGroup_    = activeGroup;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::unique_ptr<MeshBuild> MeshBuild::copyGroup(int groupIndex) const {
    if (groupIndex < 0 || groupIndex >= getGroupCount()) return {};
    auto result = std::make_unique<MeshBuild>();
    result->setActiveGroup(groupNames_[size_t(groupIndex)]);
    std::unordered_map<uint32_t, uint32_t> remap;
    for (int t = 0; t < int(triangleGroups_.size()); ++t) {
        if (triangleGroups_[size_t(t)] != groupIndex) continue;
        uint32_t dst[3];
        for (int corner = 0; corner < 3; ++corner) {
            const uint32_t src = indices_[size_t(t) * 3u + size_t(corner)];
            auto found = remap.find(src);
            if (found == remap.end()) {
                const uint32_t next = uint32_t(result->getVertexCount());
                result->addVertex(getPositionX(int(src)), getPositionY(int(src)), getPositionZ(int(src)),
                                  getNormalX(int(src)), getNormalY(int(src)), getNormalZ(int(src)),
                                  getUvU(int(src)), getUvV(int(src)));
                remap.emplace(src, next); dst[corner] = next;
            } else dst[corner] = found->second;
        }
        result->addTriangle(dst[0], dst[1], dst[2]);
    }
    if (result->empty()) return {};
    result->meta_ = meta_;
    result->setMeta("group", groupNames_[size_t(groupIndex)]);
    return result;
}

bool MeshBuild::appendTransformed(const MeshBuild *other, float tx, float ty, float tz,
                                  float yawDegrees, float sx, float sy, float sz) {
    if (!other || other == this || other->empty() || std::fabs(sx) < 1e-8f ||
        std::fabs(sy) < 1e-8f || std::fabs(sz) < 1e-8f)
        return false;
    const uint32_t base = uint32_t(getVertexCount());
    std::vector<int> groupMap(size_t(other->getGroupCount()), -1);
    const int previousGroup = activeGroup_;
    for (int i = 0; i < other->getGroupCount(); ++i)
        groupMap[size_t(i)] = setActiveGroup(other->getGroupName(i));
    reserve(getVertexCount() + other->getVertexCount(), getIndexCount() + other->getIndexCount());
    const float radians = yawDegrees * 0.01745329251994329577f;
    const float c = std::cos(radians), s = std::sin(radians);
    for (int i = 0; i < other->getVertexCount(); ++i) {
        const float px = other->getPositionX(i) * sx;
        const float py = other->getPositionY(i) * sy;
        const float pz = other->getPositionZ(i) * sz;
        const float ix = other->getNormalX(i) / sx;
        const float iy = other->getNormalY(i) / sy;
        const float iz = other->getNormalZ(i) / sz;
        float nx = c * ix + s * iz;
        float ny = iy;
        float nz = -s * ix + c * iz;
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl > 1e-8f) { nx /= nl; ny /= nl; nz /= nl; }
        addVertex(c * px + s * pz + tx, py + ty, -s * px + c * pz + tz,
                  nx, ny, nz, other->getUvU(i), other->getUvV(i));
    }
    const bool mirrored = (sx * sy * sz) < 0.f;
    for (int i = 0; i + 2 < other->getIndexCount(); i += 3) {
        const int sourceGroup = other->getTriangleGroup(i / 3);
        activeGroup_ = sourceGroup >= 0 ? groupMap[size_t(sourceGroup)] : -1;
        const uint32_t a = base + uint32_t(other->getIndex(i));
        const uint32_t b = base + uint32_t(other->getIndex(i + 1));
        const uint32_t cidx = base + uint32_t(other->getIndex(i + 2));
        if (mirrored) addTriangle(a, cidx, b); else addTriangle(a, b, cidx);
    }
    activeGroup_ = previousGroup;
    return true;
}

int MeshBuild::getVertexCount() const { return int(positions_.size() / 3u); }
int MeshBuild::getIndexCount() const { return int(indices_.size()); }
bool MeshBuild::empty() const { return positions_.empty() || indices_.empty(); }

float MeshBuild::getPositionX(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return positions_[size_t(i) * 3u];
}
float MeshBuild::getPositionY(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return positions_[size_t(i) * 3u + 1u];
}
float MeshBuild::getPositionZ(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return positions_[size_t(i) * 3u + 2u];
}
float MeshBuild::getNormalX(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return normals_[size_t(i) * 3u];
}
float MeshBuild::getNormalY(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return normals_[size_t(i) * 3u + 1u];
}
float MeshBuild::getNormalZ(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return normals_[size_t(i) * 3u + 2u];
}
float MeshBuild::getUvU(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return uvs_[size_t(i) * 2u];
}
float MeshBuild::getUvV(int i) const {
    if (i < 0 || i >= getVertexCount()) return 0.f;
    return uvs_[size_t(i) * 2u + 1u];
}
int MeshBuild::getIndex(int i) const {
    if (i < 0 || i >= getIndexCount()) return 0;
    return int(indices_[size_t(i)]);
}

void MeshBuild::setMeta(const std::string &key, const std::string &value) { meta_[key] = value; }

std::string MeshBuild::getMeta(const std::string &key, const std::string &defaultValue) const {
    auto it = meta_.find(key);
    return it == meta_.end() ? defaultValue : it->second;
}

}  // namespace eve::procgen
