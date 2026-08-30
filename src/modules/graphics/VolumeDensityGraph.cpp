#include "graphics/VolumeDensityGraph.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace eve::graphics {
namespace {

float hash3(const glm::ivec3 &p, std::uint32_t seed) {
    std::uint32_t h = seed ^ (std::uint32_t(p.x) * 0x8da6b343u) ^
        (std::uint32_t(p.y) * 0xd8163841u) ^ (std::uint32_t(p.z) * 0xcb1ab31fu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return float(h & 0x00ffffffu) / float(0x01000000u);
}

}  // namespace

void VolumeDensityGraph::clear() {
    nodes_.clear();
    output_ = -1;
}

int VolumeDensityGraph::addNode(Op op, int inputA, int inputB, const glm::vec4 &params) {
    const int next = int(nodes_.size());
    Node node;
    node.op = op;
    node.inputA = inputA >= 0 && inputA < next ? inputA : -1;
    node.inputB = inputB >= 0 && inputB < next ? inputB : -1;
    node.params = params;
    nodes_.push_back(node);
    output_ = next;
    return next;
}

void VolumeDensityGraph::setOutput(int nodeIndex) {
    output_ = nodeIndex >= 0 && nodeIndex < int(nodes_.size()) ? nodeIndex
                                                               : int(nodes_.size()) - 1;
}

float VolumeDensityGraph::valueNoise(const glm::vec3 &p, std::uint32_t seed) {
    const glm::ivec3 cell = glm::ivec3(glm::floor(p));
    const glm::vec3 f = glm::fract(p);
    const glm::vec3 u = f * f * (3.f - 2.f * f);
    float corners[2][2][2];
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                corners[z][y][x] = hash3(cell + glm::ivec3(x, y, z), seed);
    const float z0 = glm::mix(glm::mix(corners[0][0][0], corners[0][0][1], u.x),
                              glm::mix(corners[0][1][0], corners[0][1][1], u.x), u.y);
    const float z1 = glm::mix(glm::mix(corners[1][0][0], corners[1][0][1], u.x),
                              glm::mix(corners[1][1][0], corners[1][1][1], u.x), u.y);
    return glm::mix(z0, z1, u.z);
}

float VolumeDensityGraph::evaluate(const glm::vec3 &worldPosition, float time) const {
    if (nodes_.empty()) return 0.f;
    std::vector<float> values(nodes_.size(), 0.f);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node &n = nodes_[i];
        const float a = n.inputA >= 0 ? values[std::size_t(n.inputA)] : 0.f;
        const float b = n.inputB >= 0 ? values[std::size_t(n.inputB)] : 0.f;
        switch (n.op) {
            case Op::constant: values[i] = n.params.x; break;
            case Op::height:
                values[i] = std::exp(-std::max(n.params.y, 0.f) *
                                     std::max(worldPosition.y - n.params.x, 0.f));
                break;
            case Op::sphere: {
                const float radius = std::max(n.params.w, 1e-4f);
                values[i] = std::clamp(1.f - glm::length(worldPosition - glm::vec3(n.params)) /
                                      radius, 0.f, 1.f);
                break;
            }
            case Op::box: {
                const glm::vec3 q = glm::abs(worldPosition - glm::vec3(n.params));
                const float extent = std::max(n.params.w, 1e-4f);
                values[i] = std::clamp(1.f - std::max(std::max(q.x, q.y), q.z) / extent,
                                       0.f, 1.f);
                break;
            }
            case Op::noise: {
                const float scale = std::max(std::fabs(n.params.x), 1e-4f);
                const glm::vec3 wind(n.params.y, 0.f, n.params.z);
                values[i] = valueNoise((worldPosition + wind * time) / scale,
                                       std::uint32_t(std::max(n.params.w, 0.f)));
                break;
            }
            case Op::add: values[i] = a + b; break;
            case Op::multiply: values[i] = a * b; break;
            case Op::subtract: values[i] = a - b; break;
            case Op::clamp: values[i] = std::clamp(a, n.params.x, n.params.y); break;
        }
    }
    const int selected = output_ >= 0 ? output_ : int(nodes_.size()) - 1;
    return values[std::size_t(selected)];
}

}  // namespace eve::graphics
