#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

class Heightmap;

/** @brief One deterministic sample used by script-first procedural pipelines. */
struct ProcgenPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    float normalX = 0.f;
    float normalY = 1.f;
    float normalZ = 0.f;

    float    yaw     = 0.f;
    float    scaleX  = 1.f;
    float    scaleY  = 1.f;
    float    scaleZ  = 1.f;
    float    density = 1.f;
    uint32_t seed    = 1;

    std::unordered_map<std::string, float>       floatAttributes;
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
    void  setScale(int index, float x, float y, float z);
    float getScaleX(int index) const;
    float getScaleY(int index) const;
    float getScaleZ(int index) const;

    void     setDensity(int index, float density);
    float    getDensity(int index) const;
    void     setPointSeed(int index, uint32_t seed);
    uint32_t getPointSeed(int index) const;

    void        setFloatAttribute(int index, const std::string& name, float value);
    float       getFloatAttribute(int index, const std::string& name, float fallback) const;
    bool        hasFloatAttribute(int index, const std::string& name) const;
    void        setStringAttribute(int index, const std::string& name, const std::string& value);
    std::string getStringAttribute(int index, const std::string& name, const std::string& fallback) const;
    bool        hasStringAttribute(int index, const std::string& name) const;

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

}  // namespace eve::procgen
