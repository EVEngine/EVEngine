#pragma once

#include "animation/tensor/SmrFeatures.h"
#include "common/Result.h"
#include "tensor/OnnxModel.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eve::animation {

/**
 * @brief MeshRet-compatible ONNX runner on CPU via eve::tensor::OnnxModel.
 *
 * I/O contract from scripts/export_meshret_onnx.py:
 * - inputs: source_rot6d [1,T,J,6], source_geom [1,S,7], target_geom [1,S,7], source_dmi [1,T,P,10]
 * - output: target_rot6d [1,T,J,6]
 *
 * @ownership Owns the loaded OnnxModel until load/reload or destruction.
 * @lifetime Model bytes are copied at load; feature tensors are borrowed only for run().
 * @thread Owner thread; not re-entrant.
 */
class SmrOnnxRunner {
public:
    [[nodiscard]] Result<void>               load(std::span<const uint8_t> bytes);
    [[nodiscard]] Result<void>               loadFile(const std::string& path);
    [[nodiscard]] bool                       isLoaded() const { return static_cast<bool>(model_); }
    [[nodiscard]] tensor::OnnxModelInfo      info() const;
    [[nodiscard]] Result<std::vector<float>> run(const SmrFeatureBatch& features) const;

private:
    std::unique_ptr<tensor::OnnxModel> model_;
};

}  // namespace eve::animation
