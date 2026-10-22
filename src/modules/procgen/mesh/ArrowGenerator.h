#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "procgen/MeshBuild.h"

#include <array>
#include <string_view>

namespace eve::procgen {

/**
 * @brief CPU geometry builder for A-to-B selection indicators, independent of object lifetime.
 * @ownership Caller owns the builder and returned meshes; all inputs are copied values.
 * @thread Owner-thread mutations; independent instances can run concurrently. No callbacks.
 * Geometry is transient, deterministic within floating-point tolerance (1e-5 relative).
 * Target resolution, visibility, material, depth testing and upload belong to the caller.
 */
class EVENGINE_API_DOMAINS ArrowGenerator {
public:
    /** @brief Set finite world-space endpoints; failure preserves previous endpoints.
     * @param ax A x.
     * @param ay A y.
     * @param az A z.
     * @param bx B x.
     * @param by B y.
     * @param bz B z.
     * @return InvalidArgument for coincident endpoints or coordinates outside +/-1e6.
     */
    [[nodiscard]] Result<void> setEndpoints(float ax, float ay, float az, float bx, float by, float bz);
    /** @brief Set quadratic Bezier control offset from the A/B midpoint; zero gives a straight line.
     * @param x Control offset x.
     * @param y Control offset y.
     * @param z Control offset z.
     * @return InvalidArgument for non-finite or out-of-range offsets; otherwise success.
     */
    [[nodiscard]] Result<void> setBend(float x, float y, float z);
    /** @brief Choose "2d" (XY ribbon), "3d" (closed tube/cone), or "flat" (beveled XY slab).
     * @param mode Case-sensitive mode name.
     * @return InvalidArgument for an unknown mode, preserving previous state.
     */
    [[nodiscard]] Result<void> setMode(std::string_view mode);
    /** @brief Set positive full shaft width, full head width and head length, each at most 1e6.
     * @param shaftWidth Full shaft width, at least 1e-5.
     * @param headWidth Full head width, larger than shaftWidth.
     * @param headLength Axial head length, at least 1e-5.
     * @return InvalidArgument unless head width exceeds shaft width; failure preserves state.
     */
    [[nodiscard]] Result<void> setSize(float shaftWidth, float headWidth, float headLength);
    /** @brief Configure the flat mode's thin extrusion and rounded rim.
     * @param thickness Full Z thickness, greater than twice radius and at most 1e6.
     * @param radius Bevel radius, at least 1e-5; build also requires less than one quarter shaft width.
     * @param segments Round corner and bevel subdivisions [2,12].
     * @return InvalidArgument for invalid settings; failure preserves previous state.
     */
    [[nodiscard]] Result<void> setBevel(float thickness, float radius, int segments);
    /** @brief Select flat geometry's uniform outline or tapered, softly rounded flowing profile.
     * @param profile "uniform" (default) or "flowing"; affects flat mode only.
     * Flowing tapers the tail to 8% of nominal neck width/thickness, adds up to 35% face crowning,
     * and uses angle-weighted smooth normals with a 75-degree crease policy.
     * @return InvalidArgument for an unknown name; failure preserves prior state.
     */
    [[nodiscard]] Result<void> setFlatProfile(std::string_view profile);
    /** @brief Set the flat arrow head's thickness relative to its shaft, with a short shoulder ramp.
     * @param scale Multiplier [1,4]; 1 preserves uniform thickness. Ignored by 2d/3d modes.
     * @return InvalidArgument for non-finite or out-of-range input; failure preserves prior state.
     */
    [[nodiscard]] Result<void> setHeadThicknessScale(float scale);
    /** @brief Set tessellation budgets; failure preserves state.
     * @param curveSegments Curve subdivisions [2,256].
     * @param radialSides Radial sides [3,64], unused by 2d mode.
     * @return InvalidArgument for out-of-budget resolution; otherwise success.
     */
    [[nodiscard]] Result<void> setResolution(int curveSegments, int radialSides);
    /**
     * @brief Build a new owning mesh without changing this builder or any previously built mesh.
     * @return InvalidArgument for a reversed curve, a non-XY 2d path, or a path too short for the head.
     * The head tip is exactly B in every mode; flat geometry tapers width and thickness to one geometric apex.
     * Shaft starts at A. Tube/ribbon UV u follows normalized sampled arc length;
     * flat UVs are per face.
     * @cost Tube/ribbon CPU work is linear in curve segments times radial sides. Flat bevels
     * use quadratic contour validation and up to cubic cap triangulation in contour vertices; reuse the mesh
     * until endpoints or style change, and coalesce pointer updates before building/uploading.
     */
    [[nodiscard]] Result<MeshBuild> buildMeshResult() const;

private:
    std::array<double, 3> a_{0., 0., 0.};
    std::array<double, 3> b_{1., 0., 0.};
    std::array<double, 3> bend_{};
    double                shaftWidth_         = 0.04;
    double                headWidth_          = 0.12;
    double                headLength_         = 0.2;
    int                   segments_           = 32;
    int                   sides_              = 12;
    bool                  planar_             = true;
    bool                  flat_               = false;
    bool                  flowing_            = false;
    double                thickness_          = 0.02;
    double                bevelRadius_        = 0.004;
    int                   bevelSegments_      = 4;
    double                headThicknessScale_ = 1.;
};

}  // namespace eve::procgen
