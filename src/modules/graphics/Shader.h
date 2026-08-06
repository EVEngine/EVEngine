#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

namespace eve::graphics {

/**
 * Custom GPU program.
 *
 * **2D sprites** (`newShader` / `newShaderFromSpv`):
 *   Vertex = TexturedVertex; MainTex @ set0 binding0; optional push-constant data[32].
 *
 * **3D meshes** (`newMeshShader` / `newMeshShaderFromSpv`):
 *   Vertex = MeshVertex; Frame UBO @ binding0 + albedo @ binding1 (same as mesh3d);
 *   optional push-constant data[32] for style knobs.
 */
class Shader {
public:
    static constexpr int kMaxFloats = 32;
    static constexpr uint32_t kPushConstantBytes = uint32_t(kMaxFloats * sizeof(float));

    enum class Kind { eSprite2D, eMesh3D };

    enum ShaderType {
        eCompute,
        eVertex,
        eFragment,
        eGeometry,
        eTessCtrl,
        eTessEval
    };

    Shader();
    ~Shader();

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    Kind getKind() const { return kind_; }
    void setKind(Kind k) { kind_ = k; }

    /** Reserve sequential float slots in the push-constant block. Returns start index. */
    int declareFloat(const std::string &name);
    int declareVec2(const std::string &name);
    int declareVec3(const std::string &name);
    int declareVec4(const std::string &name);
    int declareMatrix(const std::string &name);

    void sendFloat(const std::string &name, float x);
    void sendVec2(const std::string &name, float x, float y);
    void sendVec3(const std::string &name, float x, float y, float z);
    void sendVec4(const std::string &name, float x, float y, float z, float w);
    void sendMatrix(const std::string &name, const glm::mat4 &m);

    /** Low-level blob write into a declared uniform (size must match declaration). */
    int sendToVar(const std::string &name, const void *data, size_t size);
    int getFromVar(const std::string &name, void *data, size_t size) const;

    bool hasUniform(const std::string &name) const;
    int getUniformIndex(const std::string &name) const;
    int getUniformFloatCount(const std::string &name) const;

    const float *pushConstantData() const { return floats_.data(); }
    uint32_t pushConstantSize() const { return uint32_t(usedFloats_ * sizeof(float)); }
    int usedFloats() const { return usedFloats_; }

    const std::vector<uint32_t> &vertexSpirv() const { return vertSpv_; }
    const std::vector<uint32_t> &fragmentSpirv() const { return fragSpv_; }

    void setSpirv(std::vector<uint32_t> vert, std::vector<uint32_t> frag) {
        vertSpv_ = std::move(vert);
        fragSpv_ = std::move(frag);
    }

    /** Backend-private GPU object (vulkan::GpuShader*). */
    void *gpuHandle = nullptr;

private:
    struct Uniform {
        int index = 0;
        int floatCount = 0;
    };

    int declareUniform(const std::string &name, int floatCount);
    Uniform *findUniform(const std::string &name);
    const Uniform *findUniform(const std::string &name) const;

    Kind kind_ = Kind::eSprite2D;
    std::vector<uint32_t> vertSpv_;
    std::vector<uint32_t> fragSpv_;
    std::map<std::string, Uniform> uniforms_;
    std::array<float, kMaxFloats> floats_{};
    int usedFloats_ = 0;
};

}  // namespace eve::graphics
