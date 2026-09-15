#pragma once

#include "animation/tensor/SmrFeatures.h"
#include "common/Result.h"

#include <vector>

namespace eve::animation {

/** @brief Hyperparameters for the builtin MeshRet-style graph. */
struct SmrMeshRetConfig {
    int latentDim = 128;
    int ffSize    = 256;
    int numLayers = 2;
    int numHeads  = 4;
    int seed      = 9923;
};

/**
 * @brief Builtin MeshRet-style PointNet + Transformer retarget (links eve::tensor).
 * @thread Owner thread; not re-entrant.
 */
class SmrMeshRetNet {
public:
    explicit SmrMeshRetNet(SmrMeshRetConfig config = {});

    [[nodiscard]] const SmrMeshRetConfig& config() const { return config_; }

    /** @brief Owning target rot6d [T*J*6], or structured failure. */
    [[nodiscard]] Result<std::vector<float>> forward(const SmrFeatureBatch& features) const;

private:
    struct LayerWeights {
        std::vector<float> wq, wk, wv, wo, w1, b1, w2, b2, ln1g, ln1b, ln2g, ln2b;
    };

    SmrMeshRetConfig          config_;
    std::vector<float>        geomW1_, geomB1_, geomW2_, geomB2_;
    std::vector<float>        dmiW1_, dmiB1_, dmiW2_, dmiB2_;
    std::vector<float>        motionW_, motionB_;
    std::vector<float>        fuseW_, fuseB_;
    std::vector<float>        outW_, outB_;
    std::vector<LayerWeights> enc_;
    std::vector<LayerWeights> dec_;

    void initWeights();
};

}  // namespace eve::animation
