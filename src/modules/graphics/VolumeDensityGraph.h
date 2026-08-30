#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace eve::graphics {

/** @brief Compact acyclic program that evaluates procedural volume density. */
class VolumeDensityGraph {
public:
    enum class Op { constant, height, sphere, box, noise, add, multiply, subtract, clamp };

    struct Node {
        Op op = Op::constant;
        int inputA = -1;
        int inputB = -1;
        glm::vec4 params{0.f};
    };

    /** @brief Remove every node and reset the output. */
    void clear();
    /** @brief Append a node and return its stable index. Inputs must precede the node. */
    int addNode(Op op, int inputA = -1, int inputB = -1,
                const glm::vec4 &params = glm::vec4(0.f));
    /** @brief Select the node returned by evaluate; invalid indices select the last node. */
    void setOutput(int nodeIndex);
    int getOutput() const { return output_; }
    int getNodeCount() const { return int(nodes_.size()); }

    /** @brief Evaluate signed density at a world position and time. */
    float evaluate(const glm::vec3 &worldPosition, float time = 0.f) const;

private:
    static float valueNoise(const glm::vec3 &p, std::uint32_t seed);

    std::vector<Node> nodes_;
    int output_ = -1;
};

}  // namespace eve::graphics
