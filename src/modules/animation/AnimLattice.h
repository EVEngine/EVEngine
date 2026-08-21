#pragma once

#include <string>
#include <vector>

namespace eve::model3d {
class ModelData;
}

namespace eve::animation {

/**
 * @brief CPU 3D lattice scale-deformer (晶格缩放变形).
 *
 * A lattice is a box of control points (divisions x/y/z, each >= 2). Every
 * control point carries a scale and an offset; a bound vertex is deformed by
 * trilinear interpolation of the eight surrounding control points:
 *
 *   p' = origin + offset(p) + scale(p) * (p - origin)
 *
 * with scale(p) the interpolated diagonal scale and offset(p) the interpolated
 * offsets. Defaults are identity (offset 0, scale 1), so the lattice is a
 * no-op until control points are driven — perfect for procedural squash &
 * stretch / local bulge animations driven per frame from scripts.
 *
 * Vertices outside the box are clamped to the boundary cells by default
 * (setClamp(false) switches to linear extrapolation).
 *
 * Script type: `AnimLattice` (created via `anim.newLattice(divX, divY, divZ)`).
 *
 * Typical frame (script):
 *   lat.setPointScale(3, 3, 3, 2.0, 1.0, 1.0);
 *   lat.updateDeformedPositions();
 *   lat.updateDeformedNormalsFromArray(nrmArray);
 *   gfx.updateMeshVertices(mesh, lat.getDeformedPositions(),
 *                          lat.getDeformedNormals(), [], vertexCount, [], 0);
 *
 * Pipeline with skinning (C++): skinPositionsTo(pose, skinned) then
 * deformPositions(skinned.data(), out.data(), vertexCount).
 */
class AnimLattice {
public:
    static constexpr int kMinDivisions = 2;

    AnimLattice();

    /** @brief Creates a lattice with the given divisions (each >= 2). */
    explicit AnimLattice(int divX, int divY, int divZ);

    AnimLattice(const AnimLattice&)            = delete;
    AnimLattice& operator=(const AnimLattice&) = delete;

    // ---- lattice frame ----

    /** @brief Changes divisions; resets all control points to identity and clears the bind. */
    void setDivisions(int divX, int divY, int divZ);
    int  getDivisionsX() const { return divX_; }
    int  getDivisionsY() const { return divY_; }
    int  getDivisionsZ() const { return divZ_; }

    /** @brief World-space size of the lattice box (components must be > 0). */
    void  setSize(float sx, float sy, float sz);
    float getSizeX() const { return sizeX_; }
    float getSizeY() const { return sizeY_; }
    float getSizeZ() const { return sizeZ_; }

    /** @brief Center of the lattice box in model space. */
    void  setOrigin(float ox, float oy, float oz);
    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }
    float getOriginZ() const { return originZ_; }

    /**
     * @brief When true (default), vertices outside the box are clamped to the
     * boundary cells; when false, lattice coordinates extrapolate linearly.
     */
    void setClamp(bool clamp) { clamp_ = clamp; }
    bool getClamp() const { return clamp_; }

    // ---- control points ----

    /** @brief Total control point count (divX * divY * divZ). */
    int getPointCount() const { return static_cast<int>(points_.size()); }

    /** @brief Per-control-point scale (squash / stretch about the lattice origin). */
    void setPointScale(int ix, int iy, int iz, float sx, float sy, float sz);
    /** @brief Per-control-point translation offset (local bulge / pinch). */
    void setPointOffset(int ix, int iy, int iz, float dx, float dy, float dz);

    /** @brief Applies one scale to every control point (whole-lattice squash & stretch). */
    void setScale(float sx, float sy, float sz);
    /** @brief Resets every control point to identity (offset 0, scale 1). */
    void reset();

    float getPointScaleX(int ix, int iy, int iz) const;
    float getPointScaleY(int ix, int iy, int iz) const;
    float getPointScaleZ(int ix, int iy, int iz) const;
    float getPointOffsetX(int ix, int iy, int iz) const;
    float getPointOffsetY(int ix, int iy, int iz) const;
    float getPointOffsetZ(int ix, int iy, int iz) const;

    // ---- binding ----

    /**
     * @brief Builds a lattice bound to meshIndex of model (Assimp vertices).
     * Throws on null model / invalid mesh / empty mesh.
     * Returned pointer is owned by the caller / script GC.
     */
    static AnimLattice* fromModel(const model3d::ModelData* model, int meshIndex, int divX, int divY, int divZ);

    /** @brief Binds the current lattice to meshIndex of model (uses current divisions). */
    void bindModel(const model3d::ModelData* model, int meshIndex);

    /** @brief Binds a packed xyz array (count * 3 floats). Uses current divisions/size/origin. */
    void bindPositions(const float* posXYZ, int count);

    void clearBind();
    int  getVertexCount() const { return vertexCount_; }

    /** @brief Bind-pose position component for vertex v (0..vertexCount-1). */
    float getBindPositionX(int vertexIndex) const;
    float getBindPositionY(int vertexIndex) const;
    float getBindPositionZ(int vertexIndex) const;

    // ---- deformation ----

    /**
     * @brief Deforms count packed xyz positions into outPosXYZ.
     * Both buffers must hold count * 3 floats; count must equal getVertexCount()
     * when bound (unbound lattices still deform raw positions).
     */
    void deformPositions(const float* inPosXYZ, float* outPosXYZ, int count) const;

    /** @brief Convenience: deform into a vector sized count*3. */
    bool deformPositionsTo(const std::vector<float>& inPosXYZ, std::vector<float>& outPosXYZ) const;

    /**
     * @brief Transforms normals with the interpolated scale field and
     * renormalizes. posXYZ provides the (deformed) vertex positions used for
     * the lattice-cell lookup; count must equal getVertexCount() when bound.
     */
    bool deformNormals(const float* posXYZ, const float* inNrmXYZ, float* outNrmXYZ, int count) const;

    /** @brief Deform an input packed xyz array into the internal cache (size vertexCount*3). */
    bool updateDeformedPositions(const std::vector<float>& inPosXYZ);

    /** @brief Deform the bound bind positions into the internal cache. */
    bool updateDeformedPositions();

    /**
     * @brief Deform packed xyz normals (with packed positions for cell lookup)
     * into the internal normal cache.
     */
    bool updateDeformedNormals(const std::vector<float>& posXYZ, const std::vector<float>& inNrmXYZ);

    /** @brief True after a successful updateDeformedPositions(). */
    bool hasDeformedPositions() const { return deformedValid_; }

    /** @brief True after a successful updateDeformedNormals(). */
    bool hasDeformedNormals() const { return deformedNrmValid_; }

    /** @brief Cached deformed position component (requires updateDeformedPositions). */
    float getDeformedPositionX(int vertexIndex) const;
    float getDeformedPositionY(int vertexIndex) const;
    float getDeformedPositionZ(int vertexIndex) const;

    /** @brief Copy of the cached deformed positions (xyz packed; empty when not updated). */
    std::vector<float> getDeformedPositions() const;
    /** @brief Copy of the cached deformed normals (xyz packed; empty when not updated). */
    std::vector<float> getDeformedNormals() const;

private:
    struct ControlPoint {
        float ox = 0.f, oy = 0.f, oz = 0.f;
        float sx = 1.f, sy = 1.f, sz = 1.f;
    };

    void requirePoint(int ix, int iy, int iz) const;
    void requireVertex(int vertexIndex) const;
    int  pointIndex(int ix, int iy, int iz) const;
    void computeCell(float x, float y, float z, int& i0, int& j0, int& k0, float& fu, float& fv, float& fw) const;

    int   divX_    = kMinDivisions;
    int   divY_    = kMinDivisions;
    int   divZ_    = kMinDivisions;
    float sizeX_   = 1.f;
    float sizeY_   = 1.f;
    float sizeZ_   = 1.f;
    float originX_ = 0.f;
    float originY_ = 0.f;
    float originZ_ = 0.f;
    bool  clamp_   = true;

    std::vector<ControlPoint> points_;
    std::vector<float>        bindPos_;  // xyz packed
    int                       vertexCount_ = 0;

    std::vector<float> deformedPos_;
    std::vector<float> deformedNrm_;
    bool               deformedValid_    = false;
    bool               deformedNrmValid_ = false;
};

}  // namespace eve::animation
