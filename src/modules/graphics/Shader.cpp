#include "graphics/Shader.h"
#include "common/Exception.h"

#include <cstring>

namespace eve::graphics {

Shader::Shader() = default;

Shader::~Shader() = default;

int Shader::declareUniform(const std::string &name, int floatCount) {
    if (name.empty()) throw Exception("Shader::declare: empty uniform name");
    if (floatCount <= 0) throw Exception("Shader::declare: invalid float count");
    if (auto *existing = findUniform(name)) {
        if (existing->floatCount != floatCount)
            throw Exception("Shader::declare: '%s' already declared with different size", name.c_str());
        return existing->index;
    }
    if (usedFloats_ + floatCount > kMaxFloats)
        throw Exception("Shader::declare: push-constant block full (max %d floats)", kMaxFloats);
    Uniform u;
    u.index = usedFloats_;
    u.floatCount = floatCount;
    uniforms_[name] = u;
    usedFloats_ += floatCount;
    return u.index;
}

int Shader::declareFloat(const std::string &name) { return declareUniform(name, 1); }
int Shader::declareVec2(const std::string &name) { return declareUniform(name, 2); }
int Shader::declareVec3(const std::string &name) { return declareUniform(name, 3); }
int Shader::declareVec4(const std::string &name) { return declareUniform(name, 4); }
int Shader::declareMatrix(const std::string &name) { return declareUniform(name, 16); }

Shader::Uniform *Shader::findUniform(const std::string &name) {
    auto it = uniforms_.find(name);
    return it == uniforms_.end() ? nullptr : &it->second;
}

const Shader::Uniform *Shader::findUniform(const std::string &name) const {
    auto it = uniforms_.find(name);
    return it == uniforms_.end() ? nullptr : &it->second;
}

bool Shader::hasUniform(const std::string &name) const { return findUniform(name) != nullptr; }

int Shader::getUniformIndex(const std::string &name) const {
    auto *u = findUniform(name);
    if (!u) throw Exception("Shader: unknown uniform '%s'", name.c_str());
    return u->index;
}

int Shader::getUniformFloatCount(const std::string &name) const {
    auto *u = findUniform(name);
    if (!u) throw Exception("Shader: unknown uniform '%s'", name.c_str());
    return u->floatCount;
}

int Shader::sendToVar(const std::string &name, const void *data, size_t size) {
    auto *u = findUniform(name);
    if (!u) throw Exception("Shader::send: unknown uniform '%s' (declare it first)", name.c_str());
    if (!data) throw Exception("Shader::send: null data for '%s'", name.c_str());
    const size_t expected = size_t(u->floatCount) * sizeof(float);
    if (size != expected)
        throw Exception("Shader::send: '%s' expects %zu bytes, got %zu", name.c_str(), expected, size);
    std::memcpy(floats_.data() + u->index, data, size);
    return int(size);
}

int Shader::getFromVar(const std::string &name, void *data, size_t size) const {
    auto *u = findUniform(name);
    if (!u) throw Exception("Shader::get: unknown uniform '%s'", name.c_str());
    if (!data) throw Exception("Shader::get: null data for '%s'", name.c_str());
    const size_t expected = size_t(u->floatCount) * sizeof(float);
    if (size != expected)
        throw Exception("Shader::get: '%s' expects %zu bytes, got %zu", name.c_str(), expected, size);
    std::memcpy(data, floats_.data() + u->index, size);
    return int(size);
}

void Shader::sendFloat(const std::string &name, float x) { sendToVar(name, &x, sizeof(x)); }

void Shader::sendVec2(const std::string &name, float x, float y) {
    const float v[2] = {x, y};
    sendToVar(name, v, sizeof(v));
}

void Shader::sendVec3(const std::string &name, float x, float y, float z) {
    const float v[3] = {x, y, z};
    sendToVar(name, v, sizeof(v));
}

void Shader::sendVec4(const std::string &name, float x, float y, float z, float w) {
    const float v[4] = {x, y, z, w};
    sendToVar(name, v, sizeof(v));
}

void Shader::sendMatrix(const std::string &name, const glm::mat4 &m) {
    sendToVar(name, &m[0][0], sizeof(float) * 16);
}

}  // namespace eve::graphics
