#include "procgen/mesh/MeshDeformationSession.h"

#include "common/Capability.h"
#include "common/MeshDeformationCompute.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace eve::procgen {
namespace {

Result<void> sessionFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.meshDeformationSession"));
}

template <class T>
Result<T> sessionFailureValue(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.meshDeformationSession"));
}

bool validMesh(const MeshBuild& mesh) {
    if (mesh.empty() || mesh.positions().size() % 3u != 0u || mesh.normals().size() != mesh.positions().size() ||
        mesh.uvs().size() != mesh.positions().size() / 3u * 2u || mesh.indices().size() % 3u != 0u)
        return false;
    return std::all_of(mesh.indices().begin(), mesh.indices().end(),
                       [&](std::uint32_t index) { return index < static_cast<std::uint32_t>(mesh.getVertexCount()); });
}

void recalculateNormals(MeshBuild& mesh) {
    auto& normals = mesh.normals();
    std::fill(normals.begin(), normals.end(), 0.f);
    const auto& positions = mesh.positions();
    for (std::size_t i = 0; i + 2 < mesh.indices().size(); i += 3u) {
        const std::array<std::size_t, 3> v{std::size_t(mesh.indices()[i]) * 3u, std::size_t(mesh.indices()[i + 1]) * 3u,
                                           std::size_t(mesh.indices()[i + 2]) * 3u};
        const std::array<float, 3> ab{positions[v[1]] - positions[v[0]], positions[v[1] + 1] - positions[v[0] + 1],
                                      positions[v[1] + 2] - positions[v[0] + 2]};
        const std::array<float, 3> ac{positions[v[2]] - positions[v[0]], positions[v[2] + 1] - positions[v[0] + 1],
                                      positions[v[2] + 2] - positions[v[0] + 2]};
        const std::array<float, 3> cross{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                         ab[0] * ac[1] - ab[1] * ac[0]};
        for (const auto base : v)
            for (int component = 0; component < 3; ++component)
                normals[base + static_cast<std::size_t>(component)] += cross[static_cast<std::size_t>(component)];
    }
    for (std::size_t i = 0; i < normals.size(); i += 3u) {
        const float length =
            std::sqrt(normals[i] * normals[i] + normals[i + 1] * normals[i + 1] + normals[i + 2] * normals[i + 2]);
        if (length > 1e-7f) {
            normals[i] /= length;
            normals[i + 1] /= length;
            normals[i + 2] /= length;
        }
    }
}

}  // namespace

Result<void> MeshDeformationSession::initializeResult(const MeshBuild& mesh) {
    if (!validMesh(mesh))
        return sessionFailure(DiagnosticCode::InvalidArgument, "sculpting source mesh is empty or invalid", "mesh");
    original_           = mesh;
    current_            = mesh;
    surfaceEquilibrium_ = mesh.positions();
    surfaceVelocity_.assign(mesh.positions().size(), 0.f);
    selectedVertices_.assign(static_cast<std::size_t>(mesh.getVertexCount()), 0u);
    invalidateImpactVertexBlocks();
    undo_.clear();
    colliderRefreshMode_        = "manual";
    colliderRefreshInterval_    = 0.f;
    colliderRefreshAccumulator_ = 0.f;
    colliderOffsetX_ = colliderOffsetY_ = colliderOffsetZ_ = 0.f;
    colliderRefreshConfigured_                             = false;
    colliderRefreshPending_                                = false;
    initialized_                                           = true;
    ++revision_;
    return Result<void>::success();
}

void MeshDeformationSession::invalidateImpactVertexBlocks() noexcept {
    impactVertexBlocks_.clear();
    impactBlockDivisions_ = 0;
}

Result<void> MeshDeformationSession::prepareImpactVertexBlocksResult(int divisionsPerAxis) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "damage session is not initialized", "mesh");
    if (divisionsPerAxis < 1 || divisionsPerAxis > 128)
        return sessionFailure(DiagnosticCode::InvalidArgument, "impact block divisions must be in [1, 128]",
                              "divisionsPerAxis");
    const auto& positions = current_.positions();
    impactBlockMinX_ = impactBlockMaxX_ = positions[0];
    impactBlockMinY_ = impactBlockMaxY_ = positions[1];
    impactBlockMinZ_ = impactBlockMaxZ_ = positions[2];
    for (std::size_t index = 3; index < positions.size(); index += 3) {
        impactBlockMinX_ = std::min(impactBlockMinX_, positions[index]);
        impactBlockMaxX_ = std::max(impactBlockMaxX_, positions[index]);
        impactBlockMinY_ = std::min(impactBlockMinY_, positions[index + 1]);
        impactBlockMaxY_ = std::max(impactBlockMaxY_, positions[index + 1]);
        impactBlockMinZ_ = std::min(impactBlockMinZ_, positions[index + 2]);
        impactBlockMaxZ_ = std::max(impactBlockMaxZ_, positions[index + 2]);
    }
    const std::size_t blockCount = static_cast<std::size_t>(divisionsPerAxis) * divisionsPerAxis * divisionsPerAxis;
    std::vector<std::vector<std::uint32_t>> blocks(blockCount);
    const auto coordinate = [divisionsPerAxis](float value, float minimum, float maximum) {
        if (maximum - minimum <= 1e-7f) return 0;
        return std::clamp(static_cast<int>((value - minimum) / (maximum - minimum) * divisionsPerAxis), 0,
                          divisionsPerAxis - 1);
    };
    for (int vertex = 0; vertex < current_.getVertexCount(); ++vertex) {
        const auto base = static_cast<std::size_t>(vertex) * 3u;
        const int  ix = coordinate(positions[base], impactBlockMinX_, impactBlockMaxX_);
        const int  iy = coordinate(positions[base + 1], impactBlockMinY_, impactBlockMaxY_);
        const int  iz = coordinate(positions[base + 2], impactBlockMinZ_, impactBlockMaxZ_);
        blocks[static_cast<std::size_t>((iz * divisionsPerAxis + iy) * divisionsPerAxis + ix)].push_back(
            static_cast<std::uint32_t>(vertex));
    }
    impactVertexBlocks_  = std::move(blocks);
    impactBlockDivisions_ = divisionsPerAxis;
    ++revision_;
    return Result<void>::success();
}

int MeshDeformationSession::impactVertexBlockCount() const noexcept {
    return static_cast<int>(std::count_if(impactVertexBlocks_.begin(), impactVertexBlocks_.end(),
                                          [](const auto& block) { return !block.empty(); }));
}

std::vector<std::uint32_t> MeshDeformationSession::impactCandidates(float x, float y, float z, float radius) const {
    if (impactVertexBlocks_.empty()) {
        std::vector<std::uint32_t> all(static_cast<std::size_t>(current_.getVertexCount()));
        for (std::uint32_t index = 0; index < all.size(); ++index) all[index] = index;
        return all;
    }
    const auto coordinate = [this](float value, float minimum, float maximum) {
        if (maximum - minimum <= 1e-7f) return 0;
        return std::clamp(static_cast<int>((value - minimum) / (maximum - minimum) * impactBlockDivisions_), 0,
                          impactBlockDivisions_ - 1);
    };
    const int minX = coordinate(x - radius, impactBlockMinX_, impactBlockMaxX_);
    const int maxX = coordinate(x + radius, impactBlockMinX_, impactBlockMaxX_);
    const int minY = coordinate(y - radius, impactBlockMinY_, impactBlockMaxY_);
    const int maxY = coordinate(y + radius, impactBlockMinY_, impactBlockMaxY_);
    const int minZ = coordinate(z - radius, impactBlockMinZ_, impactBlockMaxZ_);
    const int maxZ = coordinate(z + radius, impactBlockMinZ_, impactBlockMaxZ_);
    std::vector<std::uint32_t> candidates;
    for (int iz = minZ; iz <= maxZ; ++iz)
        for (int iy = minY; iy <= maxY; ++iy)
            for (int ix = minX; ix <= maxX; ++ix) {
                const auto& block = impactVertexBlocks_[static_cast<std::size_t>(
                    (iz * impactBlockDivisions_ + iy) * impactBlockDivisions_ + ix)];
                candidates.insert(candidates.end(), block.begin(), block.end());
            }
    return candidates;
}

Result<void> MeshDeformationSession::applyBrushResult(std::string_view mode, float x, float y, float z, float radius,
                                                      float strength, float falloff, float directionX, float directionY,
                                                      float directionZ) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "sculpting session is not initialized", "mesh");
    if ((mode != "inflate" && mode != "dent" && mode != "flatten" && mode != "smooth" && mode != "directional") ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(radius) ||
        !std::isfinite(strength) || !std::isfinite(falloff) || radius <= 0.f || falloff <= 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid sculpting brush parameters", "brush");
    MeshBuild                                      candidate       = current_;
    auto&                                          positions       = candidate.positions();
    const auto                                     sourcePositions = current_.positions();
    const auto                                     sourceNormals   = current_.normals();
    std::vector<std::unordered_set<std::uint32_t>> neighbors(static_cast<std::size_t>(candidate.getVertexCount()));
    if (mode == "smooth") {
        for (std::size_t i = 0; i + 2 < candidate.indices().size(); i += 3u) {
            const std::array<std::uint32_t, 3> triangle{candidate.indices()[i], candidate.indices()[i + 1],
                                                        candidate.indices()[i + 2]};
            for (int corner = 0; corner < 3; ++corner) {
                neighbors[triangle[corner]].insert(triangle[(corner + 1) % 3]);
                neighbors[triangle[corner]].insert(triangle[(corner + 2) % 3]);
            }
        }
    }
    float directionLength = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
    if (mode == "directional" && directionLength < 1e-7f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "directional brush requires a direction", "direction");
    directionLength = std::max(directionLength, 1e-7f);
    for (int vertex = 0; vertex < candidate.getVertexCount(); ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        const float       dx = sourcePositions[base] - x, dy = sourcePositions[base + 1] - y,
                          dz       = sourcePositions[base + 2] - z;
        const float       distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance >= radius) continue;
        const float weight = std::pow(1.f - distance / radius, falloff) * strength;
        if (mode == "smooth") {
            const auto& adjacent = neighbors[static_cast<std::size_t>(vertex)];
            if (adjacent.empty()) continue;
            std::array<float, 3> average{};
            for (const auto neighbor : adjacent)
                for (int component = 0; component < 3; ++component)
                    average[static_cast<std::size_t>(component)] +=
                        sourcePositions[static_cast<std::size_t>(neighbor) * 3u + static_cast<std::size_t>(component)];
            for (int component = 0; component < 3; ++component)
                positions[base + static_cast<std::size_t>(component)] =
                    std::lerp(sourcePositions[base + static_cast<std::size_t>(component)],
                              average[static_cast<std::size_t>(component)] / static_cast<float>(adjacent.size()),
                              std::clamp(std::abs(weight), 0.f, 1.f));
        } else if (mode == "flatten") {
            positions[base + 1] = std::lerp(sourcePositions[base + 1], y, std::clamp(std::abs(weight), 0.f, 1.f));
        } else {
            const float sign = mode == "dent" ? -1.f : 1.f;
            const float vx   = mode == "directional" ? directionX / directionLength : sourceNormals[base];
            const float vy   = mode == "directional" ? directionY / directionLength : sourceNormals[base + 1];
            const float vz   = mode == "directional" ? directionZ / directionLength : sourceNormals[base + 2];
            positions[base] += vx * weight * sign;
            positions[base + 1] += vy * weight * sign;
            positions[base + 2] += vz * weight * sign;
        }
    }
    recalculateNormals(candidate);
    pushUndo();
    surfaceEquilibrium_ = candidate.positions();
    current_            = std::move(candidate);
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::applyBrushGpuResult(std::string_view mode, float x, float y, float z, float radius,
                                                         float strength, float falloff, float directionX,
                                                         float directionY, float directionZ) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "sculpting session is not initialized", "mesh");
    if ((mode != "inflate" && mode != "dent" && mode != "flatten" && mode != "smooth" && mode != "directional") ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(radius) ||
        !std::isfinite(strength) || !std::isfinite(falloff) || radius <= 0.f || falloff <= 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid GPU sculpting brush parameters", "brush");
    const float directionLength = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
    if (mode == "directional" && directionLength < 1e-7f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "directional brush requires a direction", "direction");
    auto* provider = eve::cap::query<IMeshDeformationCompute>();
    if (!provider)
        return sessionFailure(DiagnosticCode::Unsupported, "GPU mesh deformation provider is not installed", "backend");

    MeshDeformationComputeRequest request;
    request.positions = current_.positions();
    request.normals = current_.normals();
    request.centerX = x; request.centerY = y; request.centerZ = z; request.radius = radius;
    request.strength = strength; request.falloff = falloff;
    request.directionX = directionX; request.directionY = directionY; request.directionZ = directionZ;
    if (mode == "inflate") request.operation = MeshDeformationComputeOperation::Inflate;
    else if (mode == "dent") request.operation = MeshDeformationComputeOperation::Dent;
    else if (mode == "flatten") request.operation = MeshDeformationComputeOperation::Flatten;
    else if (mode == "directional") request.operation = MeshDeformationComputeOperation::Directional;
    else {
        request.operation = MeshDeformationComputeOperation::Smooth;
        request.targets = request.positions;
        std::vector<std::unordered_set<std::uint32_t>> neighbors(static_cast<std::size_t>(current_.getVertexCount()));
        for (std::size_t i = 0; i + 2 < current_.indices().size(); i += 3u) {
            const std::array<std::uint32_t, 3> triangle{current_.indices()[i], current_.indices()[i + 1], current_.indices()[i + 2]};
            for (int corner = 0; corner < 3; ++corner) {
                neighbors[triangle[corner]].insert(triangle[(corner + 1) % 3]);
                neighbors[triangle[corner]].insert(triangle[(corner + 2) % 3]);
            }
        }
        for (std::size_t vertex = 0; vertex < neighbors.size(); ++vertex) {
            if (neighbors[vertex].empty()) continue;
            for (std::uint32_t neighbor : neighbors[vertex])
                for (std::size_t component = 0; component < 3u; ++component)
                    request.targets[vertex * 3u + component] += current_.positions()[neighbor * 3u + component];
            for (std::size_t component = 0; component < 3u; ++component)
                request.targets[vertex * 3u + component] =
                    (request.targets[vertex * 3u + component] - current_.positions()[vertex * 3u + component]) /
                    static_cast<float>(neighbors[vertex].size());
        }
    }
    auto deformed = provider->deform(std::move(request));
    if (!deformed.ok()) return Result<void>::failure(deformed.status());
    if (deformed.value().size() != current_.positions().size() ||
        !std::all_of(deformed.value().begin(), deformed.value().end(), [](float value) { return std::isfinite(value); }))
        return sessionFailure(DiagnosticCode::Failed, "GPU returned an invalid mesh deformation", "backend");
    MeshBuild candidate = current_;
    candidate.positions() = std::move(deformed).takeValue();
    recalculateNormals(candidate);
    pushUndo();
    surfaceEquilibrium_ = candidate.positions();
    current_ = std::move(candidate);
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::applyImpactResult(float x, float y, float z, float impulseX, float impulseY,
                                                       float impulseZ, float radius, float plasticity, float hardness,
                                                       float maxDisplacement) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "damage session is not initialized", "mesh");
    const float impulseLength = std::sqrt(impulseX * impulseX + impulseY * impulseY + impulseZ * impulseZ);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(impulseLength) ||
        !std::isfinite(radius) || !std::isfinite(plasticity) || !std::isfinite(hardness) ||
        !std::isfinite(maxDisplacement) || impulseLength <= 1e-7f || radius <= 0.f || plasticity < 0.f ||
        hardness <= 0.f || maxDisplacement <= 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid mesh impact parameters", "impact");

    MeshBuild   candidate       = current_;
    auto&       positions       = candidate.positions();
    const auto  sourcePositions = current_.positions();
    const auto& baseline        = original_.positions();
    const float directionX      = impulseX / impulseLength;
    const float directionY      = impulseY / impulseLength;
    const float directionZ      = impulseZ / impulseLength;
    const auto candidates = impactCandidates(x, y, z, radius + maxDisplacement * 2.f);
    for (const std::uint32_t vertex : candidates) {
        const std::size_t base     = static_cast<std::size_t>(vertex) * 3u;
        const float       dx       = sourcePositions[base] - x;
        const float       dy       = sourcePositions[base + 1] - y;
        const float       dz       = sourcePositions[base + 2] - z;
        const float       distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance >= radius) continue;
        const float amount     = impulseLength * plasticity * std::pow(1.f - distance / radius, hardness);
        float       displacedX = sourcePositions[base] + directionX * amount - baseline[base];
        float       displacedY = sourcePositions[base + 1] + directionY * amount - baseline[base + 1];
        float       displacedZ = sourcePositions[base + 2] + directionZ * amount - baseline[base + 2];
        const float displacement =
            std::sqrt(displacedX * displacedX + displacedY * displacedY + displacedZ * displacedZ);
        if (displacement > maxDisplacement) {
            const float scale = maxDisplacement / displacement;
            displacedX *= scale;
            displacedY *= scale;
            displacedZ *= scale;
        }
        positions[base]     = baseline[base] + displacedX;
        positions[base + 1] = baseline[base + 1] + displacedY;
        positions[base + 2] = baseline[base + 2] + displacedZ;
    }
    recalculateNormals(candidate);
    pushUndo();
    surfaceEquilibrium_ = candidate.positions();
    current_            = std::move(candidate);
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::applyImpactGpuResult(float x, float y, float z, float impulseX, float impulseY,
                                                          float impulseZ, float radius, float plasticity,
                                                          float hardness, float maxDisplacement) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "damage session is not initialized", "mesh");
    const float impulseLength = std::sqrt(impulseX * impulseX + impulseY * impulseY + impulseZ * impulseZ);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(impulseLength) ||
        !std::isfinite(radius) || !std::isfinite(plasticity) || !std::isfinite(hardness) ||
        !std::isfinite(maxDisplacement) || impulseLength <= 1e-7f || radius <= 0.f || plasticity < 0.f ||
        hardness <= 0.f || maxDisplacement <= 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid GPU mesh impact parameters", "impact");
    auto* provider = eve::cap::query<IMeshDeformationCompute>();
    if (!provider)
        return sessionFailure(DiagnosticCode::Unsupported, "GPU mesh deformation provider is not installed", "backend");
    MeshDeformationComputeRequest request;
    request.operation = MeshDeformationComputeOperation::Impact;
    request.positions = current_.positions(); request.normals = current_.normals(); request.baseline = original_.positions();
    request.centerX = x; request.centerY = y; request.centerZ = z; request.radius = radius;
    request.strength = impulseLength * plasticity; request.falloff = hardness;
    request.directionX = impulseX / impulseLength; request.directionY = impulseY / impulseLength;
    request.directionZ = impulseZ / impulseLength; request.maxDisplacement = maxDisplacement;
    auto deformed = provider->deform(std::move(request));
    if (!deformed.ok()) return Result<void>::failure(deformed.status());
    if (deformed.value().size() != current_.positions().size() ||
        !std::all_of(deformed.value().begin(), deformed.value().end(), [](float value) { return std::isfinite(value); }))
        return sessionFailure(DiagnosticCode::Failed, "GPU returned invalid impact positions", "backend");
    MeshBuild candidate = current_;
    candidate.positions() = std::move(deformed).takeValue();
    recalculateNormals(candidate);
    pushUndo(); surfaceEquilibrium_ = candidate.positions(); current_ = std::move(candidate);
    invalidateImpactVertexBlocks(); ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::applySurfaceContactResult(float x, float y, float z, float normalX, float normalY,
                                                               float normalZ, float velocityX, float velocityY,
                                                               float velocityZ, float radius, float depth, float drag,
                                                               float falloff, float plasticity) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "interactive surface is not initialized", "mesh");
    const float normalLength = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(normalLength) ||
        !std::isfinite(velocityX) || !std::isfinite(velocityY) || !std::isfinite(velocityZ) || !std::isfinite(radius) ||
        !std::isfinite(depth) || !std::isfinite(drag) || !std::isfinite(falloff) || !std::isfinite(plasticity) ||
        normalLength <= 1e-7f || radius <= 0.f || depth < 0.f || drag < 0.f || falloff <= 0.f || plasticity < 0.f ||
        plasticity > 1.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid interactive surface contact", "contact");
    normalX /= normalLength;
    normalY /= normalLength;
    normalZ /= normalLength;
    const float normalVelocity = velocityX * normalX + velocityY * normalY + velocityZ * normalZ;
    const float tangentX       = velocityX - normalX * normalVelocity;
    const float tangentY       = velocityY - normalY * normalVelocity;
    const float tangentZ       = velocityZ - normalZ * normalVelocity;
    MeshBuild   candidate      = current_;
    auto        equilibrium    = surfaceEquilibrium_;
    auto&       positions      = candidate.positions();
    const auto  source         = current_.positions();
    for (int vertex = 0; vertex < candidate.getVertexCount(); ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        const float       dx = source[base] - x, dy = source[base + 1] - y, dz = source[base + 2] - z;
        const float       distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance >= radius) continue;
        const float weight = std::pow(1.f - distance / radius, falloff);
        const float moveX  = (-normalX * depth + tangentX * drag) * weight;
        const float moveY  = (-normalY * depth + tangentY * drag) * weight;
        const float moveZ  = (-normalZ * depth + tangentZ * drag) * weight;
        positions[base] += moveX;
        positions[base + 1] += moveY;
        positions[base + 2] += moveZ;
        equilibrium[base] += moveX * plasticity;
        equilibrium[base + 1] += moveY * plasticity;
        equilibrium[base + 2] += moveZ * plasticity;
    }
    recalculateNormals(candidate);
    pushUndo();
    current_            = std::move(candidate);
    surfaceEquilibrium_ = std::move(equilibrium);
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::recoverSurfaceResult(float dt, float recoveryRate) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "interactive surface is not initialized", "mesh");
    if (!std::isfinite(dt) || !std::isfinite(recoveryRate) || dt < 0.f || recoveryRate <= 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid interactive surface recovery step", "recovery");
    if (dt == 0.f) return Result<void>::success();
    MeshBuild   candidate = current_;
    auto&       positions = candidate.positions();
    const float blend     = 1.f - std::exp(-recoveryRate * dt);
    bool        changed   = false;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const float next = std::lerp(positions[i], surfaceEquilibrium_[i], blend);
        changed          = changed || std::abs(next - positions[i]) > 1e-7f;
        positions[i]     = next;
    }
    if (!changed) return Result<void>::success();
    recalculateNormals(candidate);
    current_ = std::move(candidate);
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::applySlimeImpulseResult(float x, float y, float z, float impulseX, float impulseY,
                                                             float impulseZ, float radius, float falloff) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "mesh slime is not initialized", "mesh");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(impulseX) ||
        !std::isfinite(impulseY) || !std::isfinite(impulseZ) || !std::isfinite(radius) || !std::isfinite(falloff) ||
        radius <= 0.f || falloff <= 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid mesh slime impulse", "slime.impulse");
    auto        candidateVelocity = surfaceVelocity_;
    const auto& positions         = current_.positions();
    for (int vertex = 0; vertex < current_.getVertexCount(); ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        const float       dx = positions[base] - x, dy = positions[base + 1] - y, dz = positions[base + 2] - z;
        const float       distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance >= radius) continue;
        const float weight = std::pow(1.f - distance / radius, falloff);
        candidateVelocity[base] += impulseX * weight;
        candidateVelocity[base + 1] += impulseY * weight;
        candidateVelocity[base + 2] += impulseZ * weight;
    }
    pushUndo();
    surfaceVelocity_ = std::move(candidateVelocity);
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::stepSlimeResult(float dt, float stiffness, float damping, float maxSpeed) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "mesh slime is not initialized", "mesh");
    if (!std::isfinite(dt) || !std::isfinite(stiffness) || !std::isfinite(damping) || !std::isfinite(maxSpeed) ||
        dt <= 0.f || stiffness < 0.f || damping < 0.f || maxSpeed <= 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid mesh slime simulation step", "slime.step");
    MeshBuild   candidate     = current_;
    auto&       positions     = candidate.positions();
    auto        velocity      = surfaceVelocity_;
    const float dampingFactor = std::exp(-damping * dt);
    for (int vertex = 0; vertex < candidate.getVertexCount(); ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        for (int component = 0; component < 3; ++component) {
            const std::size_t index = base + static_cast<std::size_t>(component);
            velocity[index] += (surfaceEquilibrium_[index] - positions[index]) * stiffness * dt;
            velocity[index] *= dampingFactor;
        }
        const float speed = std::sqrt(velocity[base] * velocity[base] + velocity[base + 1] * velocity[base + 1] +
                                      velocity[base + 2] * velocity[base + 2]);
        if (speed > maxSpeed) {
            const float scale = maxSpeed / speed;
            velocity[base] *= scale;
            velocity[base + 1] *= scale;
            velocity[base + 2] *= scale;
        }
        positions[base] += velocity[base] * dt;
        positions[base + 1] += velocity[base + 1] * dt;
        positions[base + 2] += velocity[base + 2] * dt;
    }
    recalculateNormals(candidate);
    current_         = std::move(candidate);
    surfaceVelocity_ = std::move(velocity);
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::configureColliderRefreshResult(std::string_view mode, float intervalSeconds,
                                                                    float offsetX, float offsetY, float offsetZ) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "collider refresh session is not initialized",
                              "mesh");
    if ((mode != "once" && mode != "everyFrame" && mode != "interval" && mode != "manual") ||
        !std::isfinite(intervalSeconds) || !std::isfinite(offsetX) || !std::isfinite(offsetY) ||
        !std::isfinite(offsetZ) || (mode == "interval" && intervalSeconds <= 0.f) || intervalSeconds < 0.f)
        return sessionFailure(DiagnosticCode::InvalidArgument, "invalid collider refresh configuration",
                              "colliderRefresh");
    colliderRefreshMode_        = mode;
    colliderRefreshInterval_    = intervalSeconds;
    colliderRefreshAccumulator_ = 0.f;
    colliderOffsetX_            = offsetX;
    colliderOffsetY_            = offsetY;
    colliderOffsetZ_            = offsetZ;
    colliderRefreshConfigured_  = true;
    colliderRefreshPending_     = mode == "once";
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::requestColliderRefreshResult() {
    if (!initialized_ || !colliderRefreshConfigured_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "collider refresh is not configured",
                              "colliderRefresh");
    colliderRefreshPending_ = true;
    return Result<void>::success();
}

Result<bool> MeshDeformationSession::updateColliderRefreshResult(float dt) {
    if (!initialized_ || !colliderRefreshConfigured_)
        return sessionFailureValue<bool>(DiagnosticCode::PreconditionViolation, "collider refresh is not configured",
                                         "colliderRefresh");
    if (!std::isfinite(dt) || dt < 0.f)
        return sessionFailureValue<bool>(DiagnosticCode::InvalidArgument, "collider refresh dt must be non-negative",
                                         "colliderRefresh.dt");
    if (colliderRefreshMode_ == "everyFrame") return Result<bool>::success(true);
    if (colliderRefreshMode_ == "interval") {
        colliderRefreshAccumulator_ += dt;
        const float tolerance = std::max(1e-6f, colliderRefreshInterval_ * 1e-6f);
        if (colliderRefreshAccumulator_ + tolerance < colliderRefreshInterval_) return Result<bool>::success(false);
        colliderRefreshAccumulator_ = std::fmod(colliderRefreshAccumulator_, colliderRefreshInterval_);
        if (colliderRefreshAccumulator_ + tolerance >= colliderRefreshInterval_) colliderRefreshAccumulator_ = 0.f;
        return Result<bool>::success(true);
    }
    const bool refresh      = colliderRefreshPending_;
    colliderRefreshPending_ = false;
    return Result<bool>::success(refresh);
}

Result<MeshBuild> MeshDeformationSession::colliderMeshResult() const {
    if (!initialized_ || !colliderRefreshConfigured_)
        return sessionFailureValue<MeshBuild>(DiagnosticCode::PreconditionViolation,
                                              "collider refresh is not configured", "colliderRefresh");
    MeshBuild collider  = current_;
    auto&     positions = collider.positions();
    for (std::size_t i = 0; i + 2u < positions.size(); i += 3u) {
        positions[i] += colliderOffsetX_;
        positions[i + 1u] += colliderOffsetY_;
        positions[i + 2u] += colliderOffsetZ_;
    }
    collider.setMeta("purpose", "dynamicCollider");
    collider.setMeta("refreshMode", colliderRefreshMode_);
    return Result<MeshBuild>::success(std::move(collider));
}

Result<int> MeshDeformationSession::selectVerticesSphereResult(float x, float y, float z, float radius, bool replace) {
    if (!initialized_)
        return sessionFailureValue<int>(DiagnosticCode::PreconditionViolation, "vertex editor is not initialized",
                                        "mesh");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(radius) || radius <= 0.f)
        return sessionFailureValue<int>(DiagnosticCode::InvalidArgument, "invalid spherical vertex selection",
                                        "selection");
    auto        candidate     = replace ? std::vector<std::uint8_t>(selectedVertices_.size(), 0u) : selectedVertices_;
    const auto& positions     = current_.positions();
    const float radiusSquared = radius * radius;
    for (int vertex = 0; vertex < current_.getVertexCount(); ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        const float       dx = positions[base] - x, dy = positions[base + 1] - y, dz = positions[base + 2] - z;
        if (dx * dx + dy * dy + dz * dz <= radiusSquared) candidate[static_cast<std::size_t>(vertex)] = 1u;
    }
    selectedVertices_ = std::move(candidate);
    return Result<int>::success(selectedVertexCount());
}

Result<int> MeshDeformationSession::selectVerticesBoxResult(float minX, float minY, float minZ, float maxX, float maxY,
                                                            float maxZ, bool replace) {
    if (!initialized_)
        return sessionFailureValue<int>(DiagnosticCode::PreconditionViolation, "vertex editor is not initialized",
                                        "mesh");
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(minZ) || !std::isfinite(maxX) ||
        !std::isfinite(maxY) || !std::isfinite(maxZ) || minX > maxX || minY > maxY || minZ > maxZ)
        return sessionFailureValue<int>(DiagnosticCode::InvalidArgument, "invalid box vertex selection", "selection");
    auto        candidate = replace ? std::vector<std::uint8_t>(selectedVertices_.size(), 0u) : selectedVertices_;
    const auto& positions = current_.positions();
    for (int vertex = 0; vertex < current_.getVertexCount(); ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        if (positions[base] >= minX && positions[base] <= maxX && positions[base + 1] >= minY &&
            positions[base + 1] <= maxY && positions[base + 2] >= minZ && positions[base + 2] <= maxZ)
            candidate[static_cast<std::size_t>(vertex)] = 1u;
    }
    selectedVertices_ = std::move(candidate);
    return Result<int>::success(selectedVertexCount());
}

void MeshDeformationSession::clearVertexSelection() noexcept {
    std::fill(selectedVertices_.begin(), selectedVertices_.end(), 0u);
}

int MeshDeformationSession::selectedVertexCount() const noexcept {
    return static_cast<int>(std::count(selectedVertices_.begin(), selectedVertices_.end(), std::uint8_t{1}));
}

Result<MeshVertexSelectionCenter> MeshDeformationSession::selectedVertexCenterResult() const {
    if (!initialized_ || selectedVertexCount() == 0)
        return sessionFailureValue<MeshVertexSelectionCenter>(DiagnosticCode::PreconditionViolation,
                                                               "vertex center requires a selection", "selection");
    MeshVertexSelectionCenter center;
    const auto&               positions = current_.positions();
    int                       count     = 0;
    for (int vertex = 0; vertex < current_.getVertexCount(); ++vertex) {
        if (!selectedVertices_[static_cast<std::size_t>(vertex)]) continue;
        const auto base = static_cast<std::size_t>(vertex) * 3u;
        center.x += positions[base];
        center.y += positions[base + 1];
        center.z += positions[base + 2];
        ++count;
    }
    center.x /= static_cast<float>(count);
    center.y /= static_cast<float>(count);
    center.z /= static_cast<float>(count);
    return Result<MeshVertexSelectionCenter>::success(center);
}

Result<void> MeshDeformationSession::moveSelectedVerticesResult(float x, float y, float z) {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "vertex editor is not initialized", "mesh");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return sessionFailure(DiagnosticCode::InvalidArgument, "vertex move delta must be finite", "move");
    if (selectedVertexCount() == 0)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "vertex move requires a selection", "selection");
    MeshBuild candidate = current_;
    auto&     positions = candidate.positions();
    for (int vertex = 0; vertex < candidate.getVertexCount(); ++vertex) {
        if (!selectedVertices_[static_cast<std::size_t>(vertex)]) continue;
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        positions[base] += x;
        positions[base + 1] += y;
        positions[base + 2] += z;
    }
    recalculateNormals(candidate);
    pushUndo();
    surfaceEquilibrium_ = candidate.positions();
    current_            = std::move(candidate);
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::manipulateSelectedVerticesResult(std::string_view mode, float originX,
                                                                      float originY, float originZ, float directionX,
                                                                      float directionY, float directionZ,
                                                                      float distance) {
    if (mode != "pull" && mode != "push" && mode != "grab")
        return sessionFailure(DiagnosticCode::InvalidArgument, "vertex manipulation mode must be pull, push, or grab",
                              "mode");
    if (!std::isfinite(originX) || !std::isfinite(originY) || !std::isfinite(originZ) || !std::isfinite(directionX) ||
        !std::isfinite(directionY) || !std::isfinite(directionZ) || !std::isfinite(distance))
        return sessionFailure(DiagnosticCode::InvalidArgument, "vertex manipulation values must be finite", "input");
    if (mode == "grab")
        return moveSelectedVerticesResult(directionX * distance, directionY * distance, directionZ * distance);
    if (!initialized_ || selectedVertexCount() == 0)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "vertex manipulation requires a selection",
                              "selection");
    MeshBuild   candidate = current_;
    auto&       positions = candidate.positions();
    const float sign      = mode == "push" ? -1.f : 1.f;
    for (int vertex = 0; vertex < candidate.getVertexCount(); ++vertex) {
        if (!selectedVertices_[static_cast<std::size_t>(vertex)]) continue;
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        float dx = positions[base] - originX, dy = positions[base + 1] - originY, dz = positions[base + 2] - originZ;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length < 1e-7f) continue;
        positions[base] += dx / length * distance * sign;
        positions[base + 1] += dy / length * distance * sign;
        positions[base + 2] += dz / length * distance * sign;
    }
    recalculateNormals(candidate);
    pushUndo();
    surfaceEquilibrium_ = candidate.positions();
    current_            = std::move(candidate);
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::restoreResult() {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "sculpting session is not initialized", "mesh");
    current_            = original_;
    surfaceEquilibrium_ = original_.positions();
    surfaceVelocity_.assign(surfaceEquilibrium_.size(), 0.f);
    undo_.clear();
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::bakeResult() {
    if (!initialized_)
        return sessionFailure(DiagnosticCode::PreconditionViolation, "sculpting session is not initialized", "mesh");
    original_           = current_;
    surfaceEquilibrium_ = current_.positions();
    surfaceVelocity_.assign(surfaceEquilibrium_.size(), 0.f);
    undo_.clear();
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshDeformationSession::undoResult() {
    if (undo_.empty()) return sessionFailure(DiagnosticCode::NotFound, "sculpting undo history is empty", "undo");
    current_            = std::move(undo_.back().mesh);
    surfaceEquilibrium_ = std::move(undo_.back().equilibrium);
    surfaceVelocity_    = std::move(undo_.back().velocity);
    undo_.pop_back();
    invalidateImpactVertexBlocks();
    ++revision_;
    return Result<void>::success();
}

Result<MeshBuild> MeshDeformationSession::currentMeshResult() const {
    if (!initialized_)
        return sessionFailureValue<MeshBuild>(DiagnosticCode::PreconditionViolation,
                                              "sculpting session is not initialized", "mesh");
    return Result<MeshBuild>::success(current_);
}

void MeshDeformationSession::pushUndo() {
    constexpr std::size_t historyLimit = 32;
    if (undo_.size() == historyLimit) undo_.erase(undo_.begin());
    undo_.push_back({current_, surfaceEquilibrium_, surfaceVelocity_});
}

}  // namespace eve::procgen
