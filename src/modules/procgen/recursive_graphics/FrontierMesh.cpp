#include "procgen/recursive_graphics/FrontierMesh.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <new>
#include "procgen/MeshBuild.h"
#include "procgen/recursive/RecursiveInstance.h"

namespace eve::procgen::recursive_graphics {
FrontierMesh::FrontierMesh()                                   = default;
FrontierMesh::~FrontierMesh()                                  = default;
FrontierMesh::FrontierMesh(FrontierMesh&&) noexcept            = default;
FrontierMesh& FrontierMesh::operator=(FrontierMesh&&) noexcept = default;
namespace {
Result<FrontierMesh> failure(DiagnosticCode code, const char* message) {
    return Result<FrontierMesh>::failure(Diagnostic::error(code, message, "procgen.recursive_graphics"));
}
}  // namespace
Result<FrontierMesh> buildFrontierMesh(const recursive::RecursiveInstance& instance,
                                       std::span<const recursive::NodeKey> keys, const FrontierMeshPolicy& policy) {
    if (policy.maxNodes == 0 || policy.maxNodes > static_cast<std::size_t>(INT_MAX / 36) ||
        policy.scaleExponent < -1800 || policy.scaleExponent > 1800 || !std::isfinite(policy.maxCoordinateError) ||
        policy.maxCoordinateError < 0 ||
        !std::all_of(policy.origin.begin(), policy.origin.end(), [](double x) { return std::isfinite(x); }))
        return failure(DiagnosticCode::InvalidArgument, "Invalid mesh projection or budget");
    auto frontier = instance.prepareFrontier(keys, policy.maxNodes);
    if (!frontier.ok()) return Result<FrontierMesh>::failure(frontier.status());
    try {
        FrontierMesh output;
        output.projection = policy;
        output.mesh       = std::make_unique<MeshBuild>();
        output.mesh->reserve(static_cast<int>(frontier.value().size() * 24),
                             static_cast<int>(frontier.value().size() * 36));
        output.boxNodePaths.reserve(frontier.value().size());
        for (const auto& node : frontier.value()) {
            output.revision       = node.revision;
            output.lengthExponent = node.nominal.lengthExponent;
            if (node.massFraction == 0) continue;
            std::array<std::array<float, 2>, 3> bounds{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                for (unsigned side = 0; side < 2; ++side) {
                    const double local    = side == 0 ? node.boundsMin[axis] : node.boundsMax[axis];
                    const double relative = local - policy.origin[axis];
                    const double value    = std::ldexp(relative, policy.scaleExponent);
                    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max() ||
                        (relative != 0 && value == 0))
                        return failure(DiagnosticCode::Unsupported, "Projected bounds exceed mesh range");
                    float rounded = static_cast<float>(value);
                    if ((side == 0 && rounded > value) || (side == 1 && rounded < value))
                        rounded = std::nextafter(rounded, side == 0 ? -std::numeric_limits<float>::infinity()
                                                                    : std::numeric_limits<float>::infinity());
                    const double error = std::abs(static_cast<double>(rounded) - value);
                    if (!std::isfinite(rounded) || error > policy.maxCoordinateError)
                        return failure(DiagnosticCode::Unsupported, "Mesh coordinate error exceeds budget");
                    output.maxObservedCoordinateError = std::max(output.maxObservedCoordinateError, error);
                    bounds[axis][side]                = rounded;
                }
                if (!(bounds[axis][0] < bounds[axis][1]))
                    return failure(DiagnosticCode::Unsupported, "Projected box has collapsed extent");
            }
            for (unsigned axis = 0; axis < 3; ++axis) {
                const unsigned u = (axis + 1) % 3, v = (axis + 2) % 3;
                for (unsigned side = 0; side < 2; ++side) {
                    const auto base = static_cast<std::uint32_t>(output.mesh->getVertexCount());
                    for (unsigned corner = 0; corner < 4; ++corner) {
                        const unsigned       cu = corner == 1 || corner == 2 ? 1 : 0;
                        const unsigned       cv = corner >= 2 ? 1 : 0;
                        std::array<float, 3> position{}, normal{};
                        position[axis] = bounds[axis][side];
                        position[u]    = bounds[u][cu];
                        position[v]    = bounds[v][cv];
                        normal[axis]   = side == 0 ? -1.f : 1.f;
                        output.mesh->addVertex(position[0], position[1], position[2], normal[0], normal[1], normal[2],
                                               static_cast<float>(cu), static_cast<float>(cv));
                    }
                    if (side == 0) {
                        output.mesh->addTriangle(base, base + 2, base + 1);
                        output.mesh->addTriangle(base, base + 3, base + 2);
                    } else {
                        output.mesh->addTriangle(base, base + 1, base + 2);
                        output.mesh->addTriangle(base, base + 2, base + 3);
                    }
                }
            }
            output.boxNodePaths.push_back(node.nominal.key.path);
        }
        return Result<FrontierMesh>::success(std::move(output));
    } catch (const std::bad_alloc&) {
        return failure(DiagnosticCode::Failed, "Frontier mesh allocation failed");
    }
}
}  // namespace eve::procgen::recursive_graphics
