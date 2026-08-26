#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/**
 * @brief CPU triangle mesh from procedural mesh recipes (e.g. marching cubes).
 * Positions/normals are xyz-packed; uvs are st-packed; indices are triangles.
 */
class MeshBuild {
public:
    void clear();
    void reserve(int vertexCount, int indexCount);

    void addVertex(float px, float py, float pz, float nx, float ny, float nz, float u, float v);
    void addTriangle(uint32_t i0, uint32_t i1, uint32_t i2);

    /**
     * @brief Append another procedural mesh after an affine transform.
     *
     * This is the composition primitive for graph-style generation: recipe
     * outputs can be instanced, transformed and merged without a GPU round trip.
     * Normals are transformed with inverse scale and renormalized.
     *
     * @param other Source mesh; null and self references are rejected.
     * @param tx Translation on X.
     * @param ty Translation on Y.
     * @param tz Translation on Z.
     * @param yawDegrees Rotation about world Y, in degrees.
     * @param sx Scale on X; must be non-zero.
     * @param sy Scale on Y; must be non-zero.
     * @param sz Scale on Z; must be non-zero.
     * @return True when the source mesh was appended.
     */
    bool appendTransformed(const MeshBuild *other, float tx, float ty, float tz,
                           float yawDegrees, float sx, float sy, float sz);

    /** @brief Select/create the named group assigned to subsequently added triangles. */
    int setActiveGroup(const std::string &name);
    /** @brief Number of named triangle groups. */
    int getGroupCount() const;
    /** @brief Group name, or an empty string for an invalid index. */
    std::string getGroupName(int groupIndex) const;
    /** @brief Group index for a triangle (not an index-buffer element), or -1. */
    int getTriangleGroup(int triangleIndex) const;
    /**
     * @brief Copy one named group into a standalone compact mesh (caller owns).
     * @return Null for an invalid or empty group.
     */
    MeshBuild *copyGroup(int groupIndex) const;

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
    std::vector<int>                             triangleGroups_;
    std::vector<std::string>                     groupNames_;
    int                                          activeGroup_ = -1;
    std::unordered_map<std::string, std::string> meta_;
};

}  // namespace eve::procgen
