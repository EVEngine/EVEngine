#pragma once
#include "common/Export.h"


#include <cstdint>
#include <string>
#include <vector>

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

/** @brief Vulkan 后端是否就绪（设备/队列可用）。 */
bool vulkanGpgpuReady();
/** @brief 从 SPIR-V 字节码创建计算着色器。 */
EVENGINE_API_WORLD ComputeShader *vulkanNewShaderFromSpirv(const std::vector<uint32_t> &spv);
/** @brief 创建 GPU 存储/传输缓冲区；usage 为 "storage" | "vertex" 等。 */
EVENGINE_API_WORLD GpuBuffer *vulkanNewBuffer(int byteSize, const std::string &usage);
/** @brief 派发计算着色器（groupsX/Y/Z 为线程组数）。 */
EVENGINE_API_WORLD void vulkanDispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ);

}  // namespace eve::gpgpu
