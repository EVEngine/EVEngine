#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

bool vulkanGpgpuReady();
ComputeShader *vulkanNewShaderFromSpirv(const std::vector<uint32_t> &spv);
GpuBuffer *vulkanNewBuffer(int byteSize, const std::string &usage);
void vulkanDispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ);

}  // namespace eve::gpgpu
