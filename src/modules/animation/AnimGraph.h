#pragma once

#include "animation/AnimPose.h"

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
 * or clips; those must outlive the graph. Call update() once per frame and read
 * getPose(). Graph evaluation is memoized so shared subgraphs sample only once.
 */
class AnimGraph {
public:
    explicit AnimGraph(AnimSkeleton* skeleton);
    ~AnimGraph() = default;

    AnimGraph(const AnimGraph&)            = delete;
    AnimGraph& operator=(const AnimGraph&) = delete;

    int addClip(AnimClip* clip);
    int addBlend(int a, int b, float weight = 0.5f);
    int addAdditive(int base, int additive, float weight = 1.f);
    int addLayer(int base, int overlay, float weight = 1.f);
    int addOneShot(int base, int shot, float fadeIn = 0.1f, float fadeOut = 0.1f);
    int addBlendSpace1D();
    int addBlendSpace2D();

    void addBlendSpace1DPoint(int node, float x, int child);
    void addBlendSpace2DPoint(int node, float x, float y, int child);
    void setBoneMask(int node, int boneIndex, float weight, bool includeChildren = false);
    void clearBoneMask(int node);

    void setRoot(int node);
    int  getRoot() const { return root_; }
    int  getNodeCount() const { return static_cast<int>(nodes_.size()); }
    void setWeight(int node, float weight);
    void setPosition1D(int node, float x);
    void setPosition2D(int node, float x, float y);
    void setSpeed(int node, float speed);
    void trigger(int node);
    bool isOneShotActive(int node) const;

    void      update(float dt);
    AnimPose* getPose() { return &output_; }

private:
    enum class Kind { Clip, Blend, Additive, Layer, OneShot, BlendSpace1D, BlendSpace2D };
    struct Point {
        float x = 0.f, y = 0.f;
        int   child = -1;
    };
    struct Node {
        Kind               kind = Kind::Clip;
        AnimClip*          clip = nullptr;
        int                a = -1, b = -1;
        float              weight = 1.f;
        float              time   = 0.f;
        float              speed  = 1.f;
        float              x = 0.f, y = 0.f;
        float              fadeIn = 0.1f, fadeOut = 0.1f;
        bool               active = false;
        std::vector<Point> points;
        std::vector<float> mask;
        AnimPose           cache;
        unsigned           cacheGeneration = 0;
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
                                  const std::vector<float>* mask) const;

    AnimSkeleton*     skeleton_ = nullptr;
    std::vector<Node> nodes_;
    AnimPose          output_;
    AnimPose          scratch_;
    int               root_       = -1;
    unsigned          generation_ = 0;
};

}  // namespace eve::animation
