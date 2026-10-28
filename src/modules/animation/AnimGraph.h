#pragma once
#include "common/Export.h"


#include "animation/AnimPose.h"
#include "animation/AnimPoseSource.h"
#include "common/Time.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

class AnimClip;
class AnimSkeleton;

/**
 * @brief Composable runtime pose graph supporting clips, blends, additive layers,
 * per-bone masks, one-shots and 1D/2D blend spaces. Script type: `AnimGraph`.
 *
 * Nodes are stable integer handles. A graph owns runtime state but not skeletons
 * or clips; those must outlive the graph. Call advance(step) once per frame and read
 * getPose(). Graph evaluation is memoized so shared subgraphs sample only once.
 */
class EVENGINE_API_WORLD AnimGraph : public IAnimPoseSource {
public:
    /** @brief Anim graph. */
    explicit AnimGraph(AnimSkeleton* skeleton);
    /** @brief Anim graph. */
    ~AnimGraph() override = default;

    AnimGraph(const AnimGraph&)            = delete;
    AnimGraph& operator=(const AnimGraph&) = delete;

    /** @brief Adds clip. */
    int addClip(AnimClip* clip);
    /** @brief Adds blend. */
    int addBlend(int a, int b, float weight = 0.5f);
    /** @brief Adds additive. */
    int addAdditive(int base, int additive, float weight = 1.f);
    /** @brief Adds layer. */
    int addLayer(int base, int overlay, float weight = 1.f);
    /** @brief Adds one shot. */
    int addOneShot(int base, int shot, float fadeIn = 0.1f, float fadeOut = 0.1f);
    /** @brief Adds blend space 1 d. */
    int addBlendSpace1D();
    /** @brief Adds blend space 2 d. */
    int addBlendSpace2D();

    /** @brief Adds blend space 1 d point. */
    void addBlendSpace1DPoint(int node, float x, int child);
    /** @brief Adds blend space 2 d point. */
    void addBlendSpace2DPoint(int node, float x, float y, int child);
    /** @brief Sets the bone mask. */
    void setBoneMask(int node, int boneIndex, float weight, bool includeChildren = false);
    /** @brief Clears bone mask. */
    void clearBoneMask(int node);

    /** @brief Sets the root. */
    void setRoot(int node);
    /** @brief Returns the root. */
    int  getRoot() const { return root_; }
    /** @brief Returns the node count. */
    int  getNodeCount() const { return static_cast<int>(nodes_.size()); }
    /** @brief Sets the weight. */
    void setWeight(int node, float weight);
    /** @brief Sets the position 1 d. */
    void setPosition1D(int node, float x);
    /** @brief Sets the position 2 d. */
    void setPosition2D(int node, float x, float y);
    /** @brief Sets the speed. */
    void setSpeed(int node, float speed);
    /** @brief Trigger. */
    void trigger(int node);
    /** @brief True when one shot active. */
    bool isOneShotActive(int node) const;

    /**
     * @brief Set additive reference for an Additive node: "bind" or "identity".
     * Defaults to identity (sample is already a local-space delta).
     */
    [[nodiscard]] eve::Result<void> setAdditiveReference(int node, const std::string& reference);
    /** @brief Compatibility facade over setAdditiveReference. */
    bool setAdditiveReferenceCompat(int node, const std::string& reference);
    /** @brief Return "bind" / "identity", or empty for non-additive / invalid nodes. */
    std::string getAdditiveReference(int node) const;

    /** @brief Evaluate the graph using one scheduler-owned deterministic step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step) override;
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    void update(float dt);
    /** @brief Borrowed pointer accessor.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    AnimPose* getPose() override { return &output_; }
    /**
     * @brief Return the graph skeleton.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    AnimSkeleton*                     getSkeleton() const override { return skeleton_; }
    /** @brief True when current tick. */
    [[nodiscard]] bool                hasCurrentTick() const noexcept override { return hasLastTick_; }
    /** @brief Current tick. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept override { return lastTick_; }

private:
    enum class Kind { Clip, Blend, Additive, Layer, OneShot, BlendSpace1D, BlendSpace2D };
    struct Point {
        float x = 0.f, y = 0.f;
        int   child = -1;
    };
    struct Node {
        Kind                  kind = Kind::Clip;
        AnimClip*             clip = nullptr;
        int                   a = -1, b = -1;
        float                 weight = 1.f;
        float                 time   = 0.f;
        float                 speed  = 1.f;
        float                 x = 0.f, y = 0.f;
        float                 fadeIn = 0.1f, fadeOut = 0.1f;
        bool                  active            = false;
        AnimAdditiveReference additiveReference = AnimAdditiveReference::Identity;
        std::vector<Point>    points;
        std::vector<float>    mask;
        AnimPose              cache;
        AnimPose              scratch;
        unsigned              cacheGeneration = 0;
    };

    int             addNode(Kind kind);
    Node&           requireNode(int node);
    const Node&     requireNode(int node) const;
    void            requireChild(int child) const;
    const AnimPose& evaluate(int node);
    const AnimPose& evaluateBlendSpace(Node& node, bool twoDimensional);
    void            blendMasked(AnimPose& out, const AnimPose& base, const AnimPose& overlay, float weight,
                                const std::vector<float>* mask) const;
    void            applyAdditive(AnimPose& out, const AnimPose& base, const AnimPose& delta, float weight,
                                  const std::vector<float>* mask, AnimAdditiveReference reference) const;

    AnimSkeleton*     skeleton_ = nullptr;
    std::vector<Node> nodes_;
    AnimPose          output_;
    int               root_       = -1;
    unsigned          generation_ = 0;
    eve::SimulationTick lastTick_    = eve::SimulationTick::zero();
    bool                hasLastTick_ = false;

    void updateUnchecked(float dt);
};

}  // namespace eve::animation
