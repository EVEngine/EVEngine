#include "procgen/MeshBuild.h"

namespace eve::procgen {

void MeshBuild::clear() {
    positions_.clear();
    normals_.clear();
    uvs_.clear();
    indices_.clear();
    meta_.clear();
}

void MeshBuild::reserve(int vertexCount, int indexCount) {
    if (vertexCount > 0) {
        positions_.reserve(size_t(vertexCount) * 3u);
        normals_.reserve(size_t(vertexCount) * 3u);
        uvs_.reserve(size_t(vertexCount) * 2u);
    }
    if (indexCount > 0) indices_.reserve(size_t(indexCount));
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
