#include "physics/softbody/graphics/SoftBody3DRenderer.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "physics/softbody/SoftBody3D.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace eve::physics {

void SoftBody3DRenderer::setBody(SoftBody3D* body) {
    body_            = body;
    mesh_            = nullptr;
    meshVertexCount_ = 0;
    meshIndexCount_  = 0;
}

void SoftBody3DRenderer::setColor(float r, float g, float b, float a) {
    colorR_ = std::clamp(r, 0.f, 1.f);
    colorG_ = std::clamp(g, 0.f, 1.f);
    colorB_ = std::clamp(b, 0.f, 1.f);
    colorA_ = std::clamp(a, 0.f, 1.f);
}

void SoftBody3DRenderer::draw(graphics::Graphics* graphics) {
    if (!graphics || !body_ || body_->isDestroyed()) return;
    std::vector<float>    positions;
    std::vector<float>    normals;
    std::vector<float>    uv;
    std::vector<uint32_t> indices;
    positions.reserve(static_cast<size_t>(body_->getParticleCount()) * 18);
    normals.reserve(static_cast<size_t>(body_->getParticleCount()) * 18);
    uv.reserve(static_cast<size_t>(body_->getParticleCount()) * 12);
    auto addQuad = [&](int a, int b, int c, int d) {
        const glm::vec3 pa(body_->getParticleX(a), body_->getParticleY(a), body_->getParticleZ(a));
        const glm::vec3 pb(body_->getParticleX(b), body_->getParticleY(b), body_->getParticleZ(b));
        const glm::vec3 pc(body_->getParticleX(c), body_->getParticleY(c), body_->getParticleZ(c));
        const glm::vec3 normal      = glm::normalize(glm::cross(pb - pa, pc - pa));
        const uint32_t  base        = static_cast<uint32_t>(positions.size() / 3);
        const int       vertices[4] = {a, b, c, d};
        const float     tex[8]      = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
        for (int i = 0; i < 4; ++i) {
            const int p = vertices[i];
            positions.insert(positions.end(),
                             {body_->getParticleX(p), body_->getParticleY(p), body_->getParticleZ(p)});
            normals.insert(normals.end(), {normal.x, normal.y, normal.z});
            uv.insert(uv.end(), {tex[i * 2], tex[i * 2 + 1]});
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    const int cols = body_->getCols(), rows = body_->getRows(), layers = body_->getLayers();
    auto indexOf = [=](int x, int y, int z) { return (z * rows + y) * cols + x; };
    for (int z = 0; z + 1 < layers; ++z)
        for (int y = 0; y + 1 < rows; ++y) {
            addQuad(indexOf(0, y, z), indexOf(0, y, z + 1), indexOf(0, y + 1, z + 1), indexOf(0, y + 1, z));
            addQuad(indexOf(cols - 1, y, z + 1), indexOf(cols - 1, y, z), indexOf(cols - 1, y + 1, z),
                    indexOf(cols - 1, y + 1, z + 1));
        }
    for (int z = 0; z + 1 < layers; ++z)
        for (int x = 0; x + 1 < cols; ++x) {
            addQuad(indexOf(x, 0, z + 1), indexOf(x, 0, z), indexOf(x + 1, 0, z), indexOf(x + 1, 0, z + 1));
            addQuad(indexOf(x, rows - 1, z), indexOf(x, rows - 1, z + 1), indexOf(x + 1, rows - 1, z + 1),
                    indexOf(x + 1, rows - 1, z));
        }
    for (int y = 0; y + 1 < rows; ++y)
        for (int x = 0; x + 1 < cols; ++x) {
            addQuad(indexOf(x + 1, y, 0), indexOf(x, y, 0), indexOf(x, y + 1, 0), indexOf(x + 1, y + 1, 0));
            addQuad(indexOf(x, y, layers - 1), indexOf(x + 1, y, layers - 1),
                    indexOf(x + 1, y + 1, layers - 1), indexOf(x, y + 1, layers - 1));
        }
    const int vertexCount = static_cast<int>(positions.size() / 3);
    const int indexCount  = static_cast<int>(indices.size());
    if (!mesh_ || meshVertexCount_ != vertexCount || meshIndexCount_ != indexCount) {
        mesh_ = graphics->newMeshFromArrays(positions.data(), normals.data(), uv.data(), vertexCount, indices.data(),
                                            indexCount);
        meshVertexCount_ = vertexCount;
        meshIndexCount_  = indexCount;
    } else {
        graphics->updateMeshVertices(mesh_, positions.data(), normals.data(), uv.data(), vertexCount, nullptr, 0);
    }
    if (mesh_) graphics->drawMesh(mesh_, glm::mat4(1.f), nullptr, graphics::Color(colorR_, colorG_, colorB_, colorA_));
}

}  // namespace eve::physics
