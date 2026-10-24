#include "physics/bake/graphics/DeformMeshPresentation.h"
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <numbers>
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/DeformPlayback.h"
namespace eve::physics {
struct DeformMeshPresentation::Impl {
    DeformPlayback             player;
    DeformTopology             topology;
    float                      radius;
    std::vector<DeformPoint>   sampled;
    std::vector<float>         positions, normals, uv;
    std::vector<std::uint32_t> indices;
    std::vector<glm::dvec3>    accumulated;
};
namespace {
Diagnostic invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "deformMesh");
}
}  // namespace
DeformMeshPresentation::DeformMeshPresentation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
DeformMeshPresentation::~DeformMeshPresentation() = default;
DeformPlayback&                                 DeformMeshPresentation::player() noexcept { return impl_->player; }
Result<std::unique_ptr<DeformMeshPresentation>> DeformMeshPresentation::prepare(DeformTrack track, float ropeRadius) {
    using Output = Result<std::unique_ptr<DeformMeshPresentation>>;
    if (track.recipe()->coordinates() != BakeCoordinates::MetresRightHandedYUp)
        return Output::failure(invalid("3D deformation presentation requires metres"));
    const auto topology  = track.topology();
    const bool triangles = topology.primitive() == DeformPrimitive::Triangles;
    if (!triangles && (!std::isfinite(ropeRadius) || ropeRadius <= 0))
        return Output::failure(invalid("rope radius must be finite and positive"));
    const auto count      = triangles ? topology.vertexCount() * 2 : topology.indices().size() / 2 * 18;
    const auto indexCount = triangles ? topology.indices().size() * 2 : topology.indices().size() / 2 * 96;
    if (count > 2000000 || indexCount > 12000000)
        return Output::failure(invalid("deformation presentation budget exceeded"));
    auto player = DeformPlayback::prepare(track);
    if (!player) return Output::failure(player.status());
    try {
        auto impl =
            std::make_unique<Impl>(Impl{std::move(player).takeValue(), topology, ropeRadius, {}, {}, {}, {}, {}, {}});
        impl->sampled.resize(topology.vertexCount());
        impl->positions.resize(count * 3);
        impl->normals.resize(count * 3);
        impl->uv.resize(count * 2);
        impl->indices.reserve(indexCount);
        if (triangles) {
            impl->accumulated.resize(topology.vertexCount());
            for (auto index : topology.indices()) impl->indices.push_back(index);
            const auto offset = static_cast<std::uint32_t>(topology.vertexCount());
            for (std::size_t i = 0; i < topology.indices().size(); i += 3) {
                impl->indices.push_back(topology.indices()[i] + offset);
                impl->indices.push_back(topology.indices()[i + 2] + offset);
                impl->indices.push_back(topology.indices()[i + 1] + offset);
            }
            if (!topology.uv().empty())
                for (std::size_t i = 0; i < count; ++i) {
                    auto value          = topology.uv()[i % topology.vertexCount()];
                    impl->uv[i * 2]     = value.u;
                    impl->uv[i * 2 + 1] = value.v;
                }
        } else {
            for (std::uint32_t base = 0; base < count; base += 18) {
                for (std::uint32_t side = 0; side < 8; ++side) {
                    const auto next = (side + 1) % 8;
                    for (auto index :
                         {base + side, base + next, base + 8 + side, base + next, base + 8 + next, base + 8 + side,
                          base + 16, base + next, base + side, base + 17, base + 8 + side, base + 8 + next})
                        impl->indices.push_back(index);
                }
            }
        }
        return Output::success(std::unique_ptr<DeformMeshPresentation>(new DeformMeshPresentation(std::move(impl))));
    } catch (const std::bad_alloc&) {
        return Output::failure(Diagnostic::error(DiagnosticCode::Failed, "deformation mesh allocation failed"));
    }
}
Result<void> DeformMeshPresentation::rebuild() {
    auto sampled = impl_->player.sample(impl_->sampled);
    if (!sampled) return sampled;
    auto point = [&](std::uint32_t index) {
        auto p = impl_->sampled[index];
        return glm::dvec3(p.x, p.y, p.z);
    };
    bool representable = true;
    auto write         = [&](std::size_t index, glm::dvec3 p, glm::dvec3 n) {
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(p[axis]) || std::abs(p[axis]) > std::numeric_limits<float>::max() ||
                !std::isfinite(n[axis])) {
                representable = false;
                continue;
            }
            impl_->positions[index * 3 + axis] = static_cast<float>(p[axis]);
            impl_->normals[index * 3 + axis]   = static_cast<float>(n[axis]);
        }
    };
    if (impl_->topology.primitive() == DeformPrimitive::Triangles) {
        std::fill(impl_->accumulated.begin(), impl_->accumulated.end(), glm::dvec3(0));
        const auto indices = impl_->topology.indices();
        for (std::size_t i = 0; i < indices.size(); i += 3) {
            const auto normal =
                glm::cross(point(indices[i + 1]) - point(indices[i]), point(indices[i + 2]) - point(indices[i]));
            for (int corner = 0; corner < 3; ++corner) impl_->accumulated[indices[i + corner]] += normal;
        }
        for (std::size_t i = 0; i < impl_->sampled.size(); ++i) {
            auto         normal = impl_->accumulated[i];
            const double length = glm::length(normal);
            normal              = length > 0 ? normal / length : glm::dvec3(0, 1, 0);
            write(i, point(static_cast<std::uint32_t>(i)), normal);
            write(i + impl_->sampled.size(), point(static_cast<std::uint32_t>(i)), -normal);
        }
    } else {
        const auto indices = impl_->topology.indices();
        for (std::size_t i = 0; i < indices.size(); i += 2) {
            auto         a = point(indices[i]), b = point(indices[i + 1]);
            auto         axis    = b - a;
            const double length  = glm::length(axis);
            axis                 = length > 0 ? axis / length : glm::dvec3(0, 1, 0);
            const auto reference = std::abs(axis.y) < 0.9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
            const auto u = glm::normalize(glm::cross(axis, reference)), v = glm::cross(axis, u);
            const auto base = i / 2 * 18;
            for (std::size_t side = 0; side < 8; ++side) {
                const double angle  = double(side) * std::numbers::pi * 0.25;
                const auto   normal = std::cos(angle) * u + std::sin(angle) * v;
                write(base + side, a + double(impl_->radius) * normal, normal);
                write(base + 8 + side, b + double(impl_->radius) * normal, normal);
            }
            write(base + 16, a, -axis);
            write(base + 17, b, axis);
        }
    }
    if (!representable) return Result<void>::failure(invalid("deformation mesh position overflow"));
    for (float value : impl_->positions)
        if (!std::isfinite(value)) return Result<void>::failure(invalid("deformation mesh position overflow"));
    for (float value : impl_->normals)
        if (!std::isfinite(value)) return Result<void>::failure(invalid("deformation mesh normal overflow"));
    return Result<void>::success();
}
Result<graphics::Mesh*> DeformMeshPresentation::createMesh(graphics::Graphics& graphics) {
    auto ready = rebuild();
    if (!ready) return Result<graphics::Mesh*>::failure(ready.status());
    auto* mesh = graphics.newMeshFromArrays(impl_->positions.data(), impl_->normals.data(), impl_->uv.data(),
                                            static_cast<int>(impl_->positions.size() / 3), impl_->indices.data(),
                                            static_cast<int>(impl_->indices.size()));
    if (!mesh)
        return Result<graphics::Mesh*>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "deformation mesh creation failed"));
    return Result<graphics::Mesh*>::success(mesh);
}
Result<void> DeformMeshPresentation::upload(graphics::Graphics& graphics, graphics::Mesh& mesh) {
    if (!graphics.describeMesh(&mesh)) return Result<void>::failure(invalid("foreign or released deformation mesh"));
    auto ready = rebuild();
    if (!ready) return ready;
    if (!graphics.updateMeshVertices(&mesh, impl_->positions.data(), impl_->normals.data(), impl_->uv.data(),
                                     static_cast<int>(impl_->positions.size() / 3), impl_->indices.data(),
                                     static_cast<int>(impl_->indices.size())))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "deformation mesh upload failed"));
    return Result<void>::success();
}
}  // namespace eve::physics
