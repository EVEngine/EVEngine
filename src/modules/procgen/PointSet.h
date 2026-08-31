#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

class Heightmap;

/** @brief Compact three-component value used by typed point metadata. */
struct ProcgenAttributeVector {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief One deterministic sample used by script-first procedural pipelines. */
struct ProcgenPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    float normalX = 0.f;
    float normalY = 1.f;
    float normalZ = 0.f;

    float    pitch   = 0.f;
    float    yaw     = 0.f;
    float    roll    = 0.f;
    float    scaleX  = 1.f;
    float    scaleY  = 1.f;
    float    scaleZ  = 1.f;
    float    density = 1.f;
    uint32_t seed    = 1;

    float boundsMinX = 0.f;
    float boundsMinY = 0.f;
    float boundsMinZ = 0.f;
    float boundsMaxX = 0.f;
    float boundsMaxY = 0.f;
    float boundsMaxZ = 0.f;

    float colorR    = 1.f;
    float colorG    = 1.f;
    float colorB    = 1.f;
    float colorA    = 1.f;
    float steepness = 0.5f;

    std::unordered_map<std::string, float>       floatAttributes;
    std::unordered_map<std::string, std::int64_t> intAttributes;
    std::unordered_map<std::string, bool>         boolAttributes;
    std::unordered_map<std::string, ProcgenAttributeVector> vectorAttributes;
    std::unordered_map<std::string, std::string> stringAttributes;
};

/**
 * @brief Script-friendly collection of attributed 3D samples.
 *
 * Operations exposed by Procgen return new PointSet instances instead of
 * mutating their input, so named intermediate values remain inspectable after
 * a hot reload and can safely be reused by more than one pipeline branch.
 */
class PointSet {
public:
    int  getCount() const;
    bool empty() const;
    void clear();

    int   add(float x, float y, float z);
    void  setPosition(int index, float x, float y, float z);
    float getX(int index) const;
    float getY(int index) const;
    float getZ(int index) const;

    void  setNormal(int index, float x, float y, float z);
    float getNormalX(int index) const;
    float getNormalY(int index) const;
    float getNormalZ(int index) const;

    void  setYaw(int index, float yaw);
    float getYaw(int index) const;
    /** @brief Set the point's local Euler rotation in degrees. */
    void setRotation(int index, float pitch, float yaw, float roll);
    /** @brief Return the point's local pitch in degrees. */
    float getPitch(int index) const;
    /** @brief Return the point's local roll in degrees. */
    float getRoll(int index) const;
    void  setScale(int index, float x, float y, float z);
    float getScaleX(int index) const;
    float getScaleY(int index) const;
    float getScaleZ(int index) const;

    /** @brief Set local-space point bounds before scale and rotation are applied. */
    void setBounds(int index, float minX, float minY, float minZ, float maxX, float maxY,
                   float maxZ);
    /** @brief Return the local-space minimum X bound. */
    float getBoundsMinX(int index) const;
    /** @brief Return the local-space minimum Y bound. */
    float getBoundsMinY(int index) const;
    /** @brief Return the local-space minimum Z bound. */
    float getBoundsMinZ(int index) const;
    /** @brief Return the local-space maximum X bound. */
    float getBoundsMaxX(int index) const;
    /** @brief Return the local-space maximum Y bound. */
    float getBoundsMaxY(int index) const;
    /** @brief Return the local-space maximum Z bound. */
    float getBoundsMaxZ(int index) const;

    /** @brief Set the normalized linear RGBA point color. */
    void setColor(int index, float red, float green, float blue, float alpha);
    /** @brief Return the point's linear red channel. */
    float getColorR(int index) const;
    /** @brief Return the point's linear green channel. */
    float getColorG(int index) const;
    /** @brief Return the point's linear blue channel. */
    float getColorB(int index) const;
    /** @brief Return the point's linear alpha channel. */
    float getColorA(int index) const;
    /** @brief Set normalized surface steepness metadata in the inclusive range [0, 1]. */
    void  setSteepness(int index, float steepness);
    /** @brief Return normalized point steepness metadata. */
    float getSteepness(int index) const;

    void     setDensity(int index, float density);
    float    getDensity(int index) const;
    void     setPointSeed(int index, uint32_t seed);
    uint32_t getPointSeed(int index) const;

    void        setFloatAttribute(int index, const std::string& name, float value);
    float       getFloatAttribute(int index, const std::string& name, float fallback) const;
    bool        hasFloatAttribute(int index, const std::string& name) const;
    /** @brief Set one signed integer metadata value. */
    void setIntAttribute(int index, const std::string& name, std::int64_t value);
    /** @brief Read signed integer metadata or return fallback when absent. */
    std::int64_t getIntAttribute(int index, const std::string& name, std::int64_t fallback) const;
    /** @brief Test whether signed integer metadata exists. */
    bool hasIntAttribute(int index, const std::string& name) const;
    /** @brief Set one boolean metadata value. */
    void setBoolAttribute(int index, const std::string& name, bool value);
    /** @brief Read boolean metadata or return fallback when absent. */
    bool getBoolAttribute(int index, const std::string& name, bool fallback) const;
    /** @brief Test whether boolean metadata exists. */
    bool hasBoolAttribute(int index, const std::string& name) const;
    /** @brief Set one three-component metadata value. */
    void setVectorAttribute(int index, const std::string& name, float x, float y, float z);
    /** @brief Read the X component of vector metadata or return fallback when absent. */
    float getVectorAttributeX(int index, const std::string& name, float fallback) const;
    /** @brief Read the Y component of vector metadata or return fallback when absent. */
    float getVectorAttributeY(int index, const std::string& name, float fallback) const;
    /** @brief Read the Z component of vector metadata or return fallback when absent. */
    float getVectorAttributeZ(int index, const std::string& name, float fallback) const;
    /** @brief Test whether vector metadata exists. */
    bool hasVectorAttribute(int index, const std::string& name) const;
    void        setStringAttribute(int index, const std::string& name, const std::string& value);
    std::string getStringAttribute(int index, const std::string& name, const std::string& fallback) const;
    bool        hasStringAttribute(int index, const std::string& name) const;
    /** @brief Return float, int, bool, vector, string, or empty when the attribute is absent. */
    std::string getAttributeType(int index, const std::string& name) const;

    const std::vector<ProcgenPoint>& points() const { return points_; }
    std::vector<ProcgenPoint>&       points() { return points_; }

private:
    ProcgenPoint*       pointAt(int index);
    const ProcgenPoint* pointAt(int index) const;

    std::vector<ProcgenPoint> points_;
};

/** @brief Stable label-based seed derivation; independent pipeline branches do not perturb each other. */
uint32_t deriveSeed(uint32_t parent, const std::string& scope);

PointSet sampleGridPoints(int width, int depth, float spacing, uint32_t seed, float jitter);
/** @brief Bridson blue-noise (Poisson disk) samples in a width x depth area (XZ, y=0). */
PointSet poissonDiskPoints(int width, int depth, float radius, uint32_t seed, int maxPoints);
PointSet filterPointHeight(const PointSet& input, float minHeight, float maxHeight);
PointSet filterPointDensity(const PointSet& input, float minDensity, float maxDensity);
PointSet filterPointBox(const PointSet& input, float minX, float minY, float minZ, float maxX,
                        float maxY, float maxZ, bool invert);
PointSet filterPointSlope(const PointSet& input, float minDegrees, float maxDegrees);
PointSet filterPointsByPolygon(const PointSet& input, const PointSet& polygon, bool invert);
PointSet filterPointsBySplineDistance(const PointSet& input, const PointSet& controlPoints,
                                      float minDistance, float maxDistance);
PointSet excludePointRadius(const PointSet& input, float x, float z, float radius);
PointSet jitterPointPositions(const PointSet& input, uint32_t seed, float amountX, float amountZ);
PointSet selfPrunePoints(const PointSet& input, float radius);
PointSet projectPointsToHeightmap(const PointSet& input, const Heightmap& heightmap,
                                  float originX, float originZ, float cellSize,
                                  float heightScale);
PointSet samplePolylinePoints(const PointSet& controlPoints, float spacing, uint32_t seed,
                              float lateralJitter);
/** @brief Concatenate two attributed point collections while preserving order. */
PointSet mergePointSets(const PointSet& first, const PointSet& second);
/** @brief Stable union that keeps the first occurrence of equal position-and-seed identities. */
PointSet unionPointSets(const PointSet& first, const PointSet& second);
/** @brief Keep first-set points whose position-and-seed identity occurs in the second set. */
PointSet intersectPointSets(const PointSet& first, const PointSet& second);
/** @brief Remove first-set points whose position-and-seed identity occurs in the second set. */
PointSet differencePointSets(const PointSet& first, const PointSet& second);
/** @brief Apply translation, yaw rotation and non-uniform scale to points and their transforms. */
PointSet transformPointSet(const PointSet& input, float translateX, float translateY,
                           float translateZ, float yawDegrees, float scaleX, float scaleY,
                           float scaleZ);
/** @brief Apply translation, pitch/yaw/roll rotation and non-uniform scale. */
PointSet transformPointSet3D(const PointSet& input, float translateX, float translateY,
                             float translateZ, float pitchDegrees, float yawDegrees,
                             float rollDegrees, float scaleX, float scaleY, float scaleZ);
/** @brief Instantiate source points relative to targets in stable target-major order. */
PointSet copyPointsToTargets(const PointSet& source, const PointSet& targets,
                             bool inheritTargetAttributes);
/** @brief Linearly remap point density between ranges with optional output clamping. */
PointSet remapPointDensity(const PointSet& input, float inputMin, float inputMax,
                           float outputMin, float outputMax, bool clampOutput);
/** @brief Apply one scalar operation to a float metadata attribute. */
PointSet mathPointFloatAttribute(const PointSet& input, const std::string& attribute,
                                 const std::string& outputAttribute,
                                 const std::string& operation, float operand,
                                 float defaultValue);
/** @brief Select points whose named float attribute lies in an inclusive range. */
PointSet filterPointFloatAttribute(const PointSet& input, const std::string& name, float minValue,
                                   float maxValue, bool invert);
/** @brief Select points whose named string attribute equals a value. */
PointSet filterPointStringAttribute(const PointSet& input, const std::string& name,
                                    const std::string& value, bool invert);
/** @brief Deterministically keep points according to density and a root seed. */
PointSet densityCullPoints(const PointSet& input, uint32_t seed, float multiplier);

}  // namespace eve::procgen
