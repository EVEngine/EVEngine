#pragma once
#include "common/Export.h"


#include "procgen/AttributeTable.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::procgen {

class Heightmap;

/** @brief One deterministic sample used by script-first procedural pipelines. */
struct ProcgenPoint {
    /** @brief Stable non-zero identity; zero marks legacy or not-yet-assigned data. */
    std::uint64_t id = 0;

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
};

/**
 * @brief Script-friendly collection of attributed 3D samples.
 *
 * Operations exposed by Procgen return new PointSet instances instead of
 * mutating their input, so named intermediate values remain inspectable after
 * a hot reload and can safely be reused by more than one pipeline branch.
 */
class EVENGINE_API_DOMAINS PointSet {
public:
    int  getCount() const;
    bool empty() const;
    void clear();

    /** @brief Reserve point storage without changing point or attribute row counts. */
    void reserve(std::size_t count);
    /** @brief Append a point with an empty attribute row and return its row index. */
    [[nodiscard]] int appendPoint(ProcgenPoint point);
    /**
     * @brief Append a point and its attributes from another set.
     * @return New row index, or a
     * schema/range failure without partial mutation.
     */
    [[nodiscard]] Result<int> appendPointFrom(const PointSet& source, std::size_t sourceIndex);
    /** @brief Clear all metadata values on an existing point while retaining the set schema. */
    [[nodiscard]] Result<void> clearPointAttributes(std::size_t index);

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
    void setBounds(int index, float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
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
    /** @brief Return the stable point identity, or zero when it has not been assigned. */
    std::uint64_t getPointId(int index) const;
    /** @brief Assign a unique non-zero point identity without mutating on failure. */
    [[nodiscard]] Result<void> trySetPointId(int index, std::uint64_t id);
    /** @brief Fill zero identities deterministically and reject pre-existing duplicates. */
    [[nodiscard]] Result<void> assignPointIds(std::uint64_t namespaceId);

    /** @brief Canonical checked float metadata write. */
    [[nodiscard]] Result<void> trySetFloatAttribute(int index, const std::string& name, float value);
    /** @brief Compatibility-only unchecked script setter; canonical code uses trySetFloatAttribute. */
    void        setFloatAttribute(int index, const std::string& name, float value);
    float       getFloatAttribute(int index, const std::string& name, float fallback) const;
    bool        hasFloatAttribute(int index, const std::string& name) const;
    /** @brief Canonical checked signed integer metadata write. */
    [[nodiscard]] Result<void> trySetIntAttribute(int index, const std::string& name, std::int64_t value);
    /** @brief Compatibility-only unchecked script setter; canonical code uses trySetIntAttribute. */
    void setIntAttribute(int index, const std::string& name, std::int64_t value);
    /** @brief Read signed integer metadata or return the caller-provided default when absent. */
    std::int64_t getIntAttribute(int index, const std::string& name, std::int64_t fallback) const;
    /** @brief Test whether signed integer metadata exists. */
    bool hasIntAttribute(int index, const std::string& name) const;
    /** @brief Canonical checked Boolean metadata write. */
    [[nodiscard]] Result<void> trySetBoolAttribute(int index, const std::string& name, bool value);
    /** @brief Compatibility-only unchecked script setter; canonical code uses trySetBoolAttribute. */
    void setBoolAttribute(int index, const std::string& name, bool value);
    /** @brief Read boolean metadata or return the caller-provided default when absent. */
    bool getBoolAttribute(int index, const std::string& name, bool fallback) const;
    /** @brief Test whether boolean metadata exists. */
    bool hasBoolAttribute(int index, const std::string& name) const;
    /** @brief Canonical checked vector metadata write. */
    [[nodiscard]] Result<void> trySetVectorAttribute(int index, const std::string& name, float x, float y, float z);
    /** @brief Compatibility-only unchecked script setter; canonical code uses trySetVectorAttribute. */
    void setVectorAttribute(int index, const std::string& name, float x, float y, float z);
    /** @brief Read the X component of vector metadata or the caller-provided default. */
    float getVectorAttributeX(int index, const std::string& name, float fallback) const;
    /** @brief Read the Y component of vector metadata or the caller-provided default. */
    float getVectorAttributeY(int index, const std::string& name, float fallback) const;
    /** @brief Read the Z component of vector metadata or the caller-provided default. */
    float getVectorAttributeZ(int index, const std::string& name, float fallback) const;
    /** @brief Test whether vector metadata exists. */
    bool hasVectorAttribute(int index, const std::string& name) const;
    /** @brief Canonical checked string metadata write. */
    [[nodiscard]] Result<void> trySetStringAttribute(int index, const std::string& name, const std::string& value);
    /** @brief Compatibility-only unchecked script setter; canonical code uses trySetStringAttribute. */
    void        setStringAttribute(int index, const std::string& name, const std::string& value);
    std::string getStringAttribute(int index, const std::string& name, const std::string& fallback) const;
    bool        hasStringAttribute(int index, const std::string& name) const;
    /** @brief Return float, int, bool, vector, string, or empty when the attribute is absent. */
    std::string getAttributeType(int index, const std::string& name) const;

    /** @brief Borrow immutable point rows; structural ownership remains with this set. */
    const std::vector<ProcgenPoint>& points() const { return points_; }
    /** @brief Mutably access one existing point without changing row structure. */
    ProcgenPoint& mutablePoint(std::size_t index);
    /** @brief Borrow the authoritative schema-bearing attribute table. */
    const AttributeTable& attributes() const noexcept { return attributes_; }
    /**
     * @brief Borrow the single-row @Data domain attribute table (graph/set metadata).
     * Row 0 is the authoritative Data domain; empty until the first Data write.
     */
    const AttributeTable& dataAttributes() const noexcept { return dataAttributes_; }
    /** @brief Mutably access the @Data domain table; callers must keep rowCount 0 or 1. */
    AttributeTable& mutableDataAttributes() noexcept { return dataAttributes_; }
    /**
     * @brief Rename one metadata column on this set.
     * @return AttributeTable rename diagnostics without mutating on failure.
     */
    [[nodiscard]] Result<void> tryRenameAttribute(const std::string& from, const std::string& to);
    /**
     * @brief Delete one metadata column on this set.
     * @return AttributeTable remove diagnostics without mutating on failure.
     */
    [[nodiscard]] Result<void> tryDeleteAttribute(const std::string& name);
    /**
     * @brief Copy one metadata column onto another name on this set.
     * @return AttributeTable copy diagnostics without mutating on failure.
     */
    [[nodiscard]] Result<void> tryCopyAttribute(const std::string& from, const std::string& to);

private:
    ProcgenPoint*       pointAt(int index);
    const ProcgenPoint* pointAt(int index) const;

    std::vector<ProcgenPoint> points_;
    AttributeTable            attributes_;
    AttributeTable            dataAttributes_;
};

/** @brief Stable label-based seed derivation; independent pipeline branches do not perturb each other. */
EVENGINE_API_DOMAINS uint32_t deriveSeed(uint32_t parent, const std::string& scope);
/** @brief Deterministically derive a non-zero stable point identity. */
EVENGINE_API_DOMAINS std::uint64_t derivePointId(std::uint64_t namespaceId, std::uint64_t ordinal);

EVENGINE_API_DOMAINS PointSet sampleGridPoints(int width, int depth, float spacing, uint32_t seed, float jitter);
/** @brief Bridson blue-noise (Poisson disk) samples in a width x depth area (XZ, y=0). */
PointSet poissonDiskPoints(int width, int depth, float radius, uint32_t seed, int maxPoints);
EVENGINE_API_DOMAINS PointSet filterPointHeight(const PointSet& input, float minHeight, float maxHeight);
PointSet filterPointDensity(const PointSet& input, float minDensity, float maxDensity);
PointSet filterPointBox(const PointSet& input, float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                        bool invert);
PointSet filterPointSlope(const PointSet& input, float minDegrees, float maxDegrees);
PointSet filterPointsByPolygon(const PointSet& input, const PointSet& polygon, bool invert);
PointSet filterPointsBySplineDistance(const PointSet& input, const PointSet& controlPoints, float minDistance,
                                      float maxDistance);
PointSet excludePointRadius(const PointSet& input, float x, float z, float radius);
EVENGINE_API_DOMAINS PointSet jitterPointPositions(const PointSet& input, uint32_t seed, float amountX, float amountZ);
PointSet selfPrunePoints(const PointSet& input, float radius);
PointSet projectPointsToHeightmap(const PointSet& input, const Heightmap& heightmap, float originX, float originZ,
                                  float cellSize, float heightScale);
PointSet samplePolylinePoints(const PointSet& controlPoints, float spacing, uint32_t seed, float lateralJitter);
/** @brief Concatenate two attributed point collections while preserving order. */
EVENGINE_API_DOMAINS PointSet mergePointSets(const PointSet& first, const PointSet& second);
/** @brief Stable union by non-zero point id, with legacy position-and-seed fallback. */
EVENGINE_API_DOMAINS PointSet unionPointSets(const PointSet& first, const PointSet& second);
/** @brief Keep first-set points whose stable or legacy identity occurs in the second set. */
EVENGINE_API_DOMAINS PointSet intersectPointSets(const PointSet& first, const PointSet& second);
/** @brief Remove first-set points whose stable or legacy identity occurs in the second set. */
PointSet differencePointSets(const PointSet& first, const PointSet& second);
/** @brief Apply translation, yaw rotation and non-uniform scale to points and their transforms. */
EVENGINE_API_DOMAINS PointSet transformPointSet(const PointSet& input, float translateX, float translateY, float translateZ,
                           float yawDegrees, float scaleX, float scaleY, float scaleZ);
/** @brief Apply translation, pitch/yaw/roll rotation and non-uniform scale. */
EVENGINE_API_DOMAINS PointSet transformPointSet3D(const PointSet& input, float translateX, float translateY, float translateZ,
                             float pitchDegrees, float yawDegrees, float rollDegrees, float scaleX, float scaleY,
                             float scaleZ);
/** @brief Instantiate source points relative to targets in stable target-major order. */
PointSet copyPointsToTargets(const PointSet& source, const PointSet& targets, bool inheritTargetAttributes);
/** @brief Linearly remap point density between ranges with optional output clamping. */
PointSet remapPointDensity(const PointSet& input, float inputMin, float inputMax, float outputMin, float outputMax,
                           bool clampOutput);
/** @brief Return whether `name` is a known `$`-prefixed float point-field selector. */

/** @brief Ensure PointSet @Data domain has exactly one row for metadata writes. */
void ensurePointSetDataRow(PointSet& points);
[[nodiscard]] bool isPointFloatSelector(std::string_view name) noexcept;
/** @brief Return whether `name` is a valid float channel (builtin selector or metadata name). */
[[nodiscard]] bool isPointFloatChannel(std::string_view name) noexcept;
/** @brief Read a float metadata column or closed `$` point-field selector. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<float> readPointFloatChannel(const PointSet& points, int index, std::string_view name,
                                                    float defaultValue);
/** @brief Write a float metadata column or closed `$` point-field selector. */
[[nodiscard]] Result<void> writePointFloatChannel(PointSet& points, int index, std::string_view name, float value);
/** @brief Apply one scalar operation to a float metadata attribute or `$` selector. */
PointSet mathPointFloatAttribute(const PointSet& input, const std::string& attribute,
                                 const std::string& outputAttribute, const std::string& operation, float operand,
                                 float defaultValue);
/** @brief Select points whose named float attribute or `$` selector lies in an inclusive range. */
EVENGINE_API_DOMAINS PointSet filterPointFloatAttribute(const PointSet& input, const std::string& name, float minValue, float maxValue,
                                   bool invert);
/** @brief Select points whose named string attribute equals a value. */
EVENGINE_API_DOMAINS PointSet filterPointStringAttribute(const PointSet& input, const std::string& name, const std::string& value,
                                    bool invert);
/** @brief Select points whose named int attribute lies in an inclusive range. */
PointSet filterPointIntAttribute(const PointSet& input, const std::string& name, std::int64_t minValue,
                                 std::int64_t maxValue, bool invert);
/** @brief Select points whose named bool attribute equals a value. */
PointSet filterPointBoolAttribute(const PointSet& input, const std::string& name, bool value, bool invert);
/** @brief Copy one float channel or typed metadata column onto another name. */
[[nodiscard]] Result<PointSet> copyPointAttribute(const PointSet& input, const std::string& source,
                                                  const std::string& target);
/** @brief Rename one metadata column. */
[[nodiscard]] Result<PointSet> renamePointAttribute(const PointSet& input, const std::string& from,
                                                    const std::string& to);
/** @brief Delete one metadata column. */
[[nodiscard]] Result<PointSet> deletePointAttribute(const PointSet& input, const std::string& name);
/**
 * @brief Transfer one attribute from `source` onto `target` points.
 * @param mode `index`, `id`, or `nearest`.
 */
[[nodiscard]] Result<PointSet> transferPointAttribute(const PointSet& target, const PointSet& source,
                                                      const std::string& attribute, const std::string& outputAttribute,
                                                      const std::string& mode);
/** @brief Write a constant int metadata column on every point. */
PointSet setPointIntAttribute(const PointSet& input, const std::string& attribute, std::int64_t value);
/** @brief Write a constant bool metadata column on every point. */
PointSet setPointBoolAttribute(const PointSet& input, const std::string& attribute, bool value);
/** @brief Write a constant vector metadata column on every point. */
PointSet setPointVectorAttribute(const PointSet& input, const std::string& attribute, float x, float y, float z);
/** @brief Compare a float channel against an operand and write a bool metadata column. */
[[nodiscard]] Result<PointSet> comparePointFloatAttribute(const PointSet& input, const std::string& attribute,
                                                          const std::string& comparison, float operand,
                                                          const std::string& outputAttribute, float defaultValue);
/** @brief Select between two float channels using a bool metadata condition. */
[[nodiscard]] Result<PointSet> selectPointFloatAttribute(const PointSet& input, const std::string& conditionAttribute,
                                                         const std::string& trueAttribute,
                                                         const std::string& falseAttribute,
                                                         const std::string& outputAttribute, float trueDefault,
                                                         float falseDefault);
/** @brief Deterministically keep points according to density and a root seed. */
EVENGINE_API_DOMAINS PointSet densityCullPoints(const PointSet& input, uint32_t seed, float multiplier);
/**
 * @brief Remap surface slope (from normals) into density.
 *
 * Slope degrees come from acos(normalY). Values outside [minDegrees, maxDegrees]
 * clamp to the output endpoints; invert reverses the mapping.
 */
PointSet densityFromNormal(const PointSet& input, float minDegrees, float maxDegrees, float outputMin,
                           float outputMax, bool invert);
/**
 * @brief Scale and pad each point's local bounds about its center.
 *
 * Zero-extent / degenerate bounds treat |scale| as the full local size before padding.
 */
PointSet modifyPointBounds(const PointSet& input, float scaleX, float scaleY, float scaleZ, float padX, float padY,
                           float padZ);
/**
 * @brief Deterministically assign a weighted mesh path string attribute.
 *
 * Empty mesh entries are ignored. When every weight is non-positive or every mesh
 * path is empty, attributes are left unchanged.
 *
 * @ownership @p meshes and @p weights are borrowed for this synchronous call only;
 *            the function does not retain the pointers after returning.
 * @lifetime Caller must keep both arrays alive for the duration of the call.
 * @param meshes Mesh path table of length @p entryCount; may be null when @p entryCount is 0.
 * @param weights Parallel weight table of length @p entryCount; may be null when @p entryCount is 0.
 */
PointSet assignWeightedMeshAttribute(const PointSet& input, uint32_t seed, const std::string& attribute,
                                     const std::string* meshes, const float* weights, int entryCount);

/** @brief Assign partition indices from a string/int attribute (mode: value|hash). */
EVENGINE_API_DOMAINS PointSet partitionPointAttribute(const PointSet& input, const std::string& attribute,
                                 const std::string& outputAttribute, const std::string& mode);
/** @brief Write deterministic float noise into a metadata attribute. */
EVENGINE_API_DOMAINS PointSet noisePointFloatAttribute(const PointSet& input, const std::string& attribute, uint32_t seed,
                                  float frequency, float amplitude, float offset);
/** @brief Apply integer math to an int metadata column. */
PointSet mathPointIntAttribute(const PointSet& input, const std::string& attribute,
                               const std::string& outputAttribute, const std::string& operation, std::int64_t operand,
                               std::int64_t defaultValue);
/** @brief Apply vector math to a vector metadata column. */
PointSet mathPointVectorAttribute(const PointSet& input, const std::string& attribute,
                                  const std::string& outputAttribute, const std::string& operation, float operandX,
                                  float operandY, float operandZ, float defaultX, float defaultY, float defaultZ);
/** @brief Write a float into the PointSet @Data domain. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> setPointDataFloatAttribute(PointSet& points, const std::string& attribute, float value);
/** @brief Write an int into the PointSet @Data domain. */
[[nodiscard]] Result<void> setPointDataIntAttribute(PointSet& points, const std::string& attribute, std::int64_t value);
/** @brief Write a string into the PointSet @Data domain. */
[[nodiscard]] Result<void> setPointDataStringAttribute(PointSet& points, const std::string& attribute,
                                                       const std::string& value);


}  // namespace eve::procgen
