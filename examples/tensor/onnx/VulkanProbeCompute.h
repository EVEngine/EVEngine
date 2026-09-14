#pragma once
#include "tensor/OnnxCompute.h"
/** @brief Standalone hardware test provider; production uses createOnnxGpuCompute(). */
[[nodiscard]] eve::Result<std::unique_ptr<eve::tensor::OnnxCompute>> createVulkanProbeCompute();
