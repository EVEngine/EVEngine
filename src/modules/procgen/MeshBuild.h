#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/**
 * CPU triangle mesh from procedural mesh recipes (e.g. marching cubes).
 * Positions/normals are xyz-packed; uvs are st-packed; indices are triangles.
 */
class MeshBuild {
public:
    void clear();
    void reserve(int vertexCount, int indexCount);

    void addVertex(float px, float py, float pz, float nx, float ny, float nz, float u, float v);
    void addTriangle(uint32_t i0, uint32_t i1, uint32_t i2);

    int getVertexCount() const;
    int getIndexCount() const;
    bool empty() const;

    float getPositionX(int i) const;
    float getPositionY(int i) const;
    float getPositionZ(int i) const;
    float getNormalX(int i) const;
    float getNormalY(int i) const;
    float getNormalZ(int i) const;
    float getUvU(int i) const;
    float getUvV(int i) const;
    int   getIndex(int i) const;

    void        setMeta(const std::string &key, const std::string &value);
    std::string getMeta(const std::string &key, const std::string &defaultValue) const;

    const std::vector<float>    &positions() const { return positions_; }
    const std::vector<float>    &normals() const { return normals_; }
    const std::vector<float>    &uvs() const { return uvs_; }
    const std::vector<uint32_t> &indices() const { return indices_; }

    std::vector<float>    &positions() { return positions_; }
    std::vector<float>    &normals() { return normals_; }
    std::vector<float>    &uvs() { return uvs_; }
    std::vector<uint32_t> &indices() { return indices_; }

private:
    std::vector<float>                           positions_;
    std::vector<float>                           normals_;
    std::vector<float>                           uvs_;
    std::vector<uint32_t>                        indices_;
    std::unordered_map<std::string, std::string> meta_;
};

}  // namespace eve::procgen
