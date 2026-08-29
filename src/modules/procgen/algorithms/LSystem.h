#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve::procgen {

class PointSet;

/**
 * @brief One drawn segment produced by an L-system turtle.
 *
 * Non-leaf segments are branch limbs (start -> end with end radii); leaf
 * segments carry a foliage marker (position = end, direction = dx/dy/dz).
 */
struct LSystemSegment {
    float sx = 0.f, sy = 0.f, sz = 0.f;  // start
    float ex = 0.f, ey = 0.f, ez = 0.f;  // end
    float r0 = 0.f, r1 = 0.f;            // radius at start / end
    int   depth = 0;                     // bracket nesting at draw time
    bool  leaf = false;                  // foliage marker instead of a limb
    float leafSize = 0.f;                // foliage card size for leaf segments
    float dx = 0.f, dy = 0.f, dz = 0.f;  // turtle heading at a leaf marker
};

/** @brief Full result of expanding an L-system grammar and tracing the turtle. */
struct LSystemResult {
    std::vector<LSystemSegment> segments;
    std::string                 derivation;
    int                         leafCount = 0;
};

/**
 * @brief General stochastic bracketed L-system engine.
 *
 * A starting axiom is rewritten `iterations` times using production rules; a
 * symbol may carry several weighted productions (chosen deterministically from
 * the seed). The derived word is then interpreted by a 3D turtle:
 *
 *   F       draw forward one step      f       move forward, no draw
 *   + / -   yaw (heading around up)    & / ^   pitch (down / up)
 *   \ / /   roll around the heading    [  ]    push / pop turtle state
 *   <leaf>  emit a leaf (see setLeafSymbols)
 *
 * Limb thickness falls off with bracket depth, so [ ] bracketing produces
 * tapering branches. The engine is not tied to mesh output: call toPointSet()
 * to recover segments as spline control points (roads, 2D layouts) or run the
 * mesh.lsystem recipe for tree/plant meshes. A fixed seed reproduces the exact
 * same result.
 */
class LSystem {
public:
    LSystem();

    /** @brief Set the starting string. @param axiom Axiom. */
    void setAxiom(const std::string& axiom);
    /** @brief Add a deterministic production for one symbol. @param symbol Source symbol. @param production Replacement. */
    void addRule(char symbol, const std::string& production);
    /** @brief Add weighted (stochastic) productions for one symbol. @param symbol Source symbol. @param productions Replacements. @param weights Selection weights. */
    void addRules(char symbol, const std::vector<std::string>& productions, const std::vector<float>& weights);
    /** @brief Remove all production rules (the axiom and alphabet remain). */
    void clearRules();

    /** @brief Set the turtle turn angle. @param degrees Angle in degrees. */
    void setAngle(float degrees);
    /** @brief Set the forward distance per F. @param step Step length. */
    void setStep(float step);
    /** @brief Set the grammar expansion count. @param iterations Rewrite count. */
    void setIterations(int iterations);
    /** @brief Set the deterministic seed. @param seed Seed. */
    void setSeed(uint32_t seed);
    /** @brief Set the initial heading (normalized). @param x X. @param y Y. @param z Z. */
    void setInitialHeading(float x, float y, float z);
    /** @brief Set the trunk radius at depth 0. @param radius Radius. */
    void setBranchRadius(float radius);
    /** @brief Set the per-depth radius multiplier. @param factor Factor in (0, 1]. */
    void setBranchRadiusFalloff(float factor);
    /** @brief Set the foliage card size. @param size Size. */
    void setLeafSize(float size);
    /** @brief Set the set of symbols that emit a leaf marker. @param symbols Character list, e.g. "L@". */
    void setLeafSymbols(const std::string& symbols);
    /** @brief Bias growth toward a direction (phototropism). @param x X. @param y Y. @param z Z. */
    void setTropism(float x, float y, float z);

    /** @brief Current seed. @return Seed. */
    uint32_t getSeed() const;
    /** @brief Current iteration count. @return Iterations. */
    int      getIterations() const;

    /** @brief Expand the grammar `iterations` times. @return The derived word. */
    std::string derive() const;
    /** @brief Expand then interpret with the turtle. @param out Result to fill. */
    void generate(LSystemResult& out) const;
    /** @brief Generate and pack segment endpoints into a PointSet. @param out Set to fill. */
    void toPointSet(PointSet& out) const;

private:
    struct Vec3 {
        float x = 0.f, y = 0.f, z = 0.f;
    };
    struct TurtleState {
        Vec3  pos, heading, up;
        int   depth = 0;
    };
    using Rule = std::pair<std::string, float>;  // production, weight

    Vec3   rotate(const Vec3& v, const Vec3& axis, float angle) const;
    void   orthonormalize(Vec3& heading, Vec3& up) const;
    void   interpret(const std::string& word, LSystemResult& out) const;

    std::string                  axiom_;
    std::vector<Rule>            rules_[256];
    float       angleDeg_    = 25.f;
    float       step_        = 1.f;
    int         iterations_  = 5;
    uint32_t    seed_        = 1;
    Vec3        heading0_    = {0.f, 1.f, 0.f};
    float       branchRadius_ = 0.1f;
    float       radiusFalloff_ = 0.6f;
    float       leafSize_    = 0.4f;
    bool        leafChars_[256] = {};
    Vec3        tropism_     = {0.f, 0.f, 0.f};
};

}  // namespace eve::procgen