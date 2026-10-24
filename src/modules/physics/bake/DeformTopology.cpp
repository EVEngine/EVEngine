#include "physics/bake/DeformTopology.h"
#include <bit>
#include <cmath>
#include <string>
#include <utility>
#include "physics/bake/BakedAssetFile.h"
namespace eve::physics {
struct DeformTopology::Data {
    DeformPrimitive            primitive;
    std::size_t                vertexCount;
    std::vector<std::uint32_t> indices;
    std::vector<DeformUV>      uv;
    std::string                key;
};
DeformTopology::DeformTopology(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
Result<DeformTopology> DeformTopology::prepare(DeformPrimitive primitive, std::size_t vertexCount,
                                               std::vector<std::uint32_t> indices, std::vector<DeformUV> uv) {
    auto failure = [] {
        return Result<DeformTopology>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid deformation topology", "deformTopology"));
    };
    if (primitive != DeformPrimitive::Segments && primitive != DeformPrimitive::Triangles) return failure();
    const std::size_t arity = primitive == DeformPrimitive::Segments ? 2 : 3;
    if (vertexCount == 0 || vertexCount > 1048576 || indices.empty() || indices.size() > 6291456 ||
        indices.size() % arity != 0 || (!uv.empty() && uv.size() != vertexCount))
        return failure();
    for (std::size_t offset = 0; offset < indices.size(); offset += arity) {
        for (std::size_t corner = 0; corner < arity; ++corner) {
            if (indices[offset + corner] >= vertexCount) return failure();
            for (std::size_t prior = 0; prior < corner; ++prior)
                if (indices[offset + prior] == indices[offset + corner]) return failure();
        }
    }
    for (auto& coordinate : uv) {
        if (!std::isfinite(coordinate.u) || !std::isfinite(coordinate.v)) return failure();
        if (coordinate.u == 0) coordinate.u = 0;  // Normalize signed zero in canonical identity.
        if (coordinate.v == 0) coordinate.v = 0;
    }
    std::string bytes = "EVEDEFT1";
    bytes.reserve(24 + indices.size() * 4 + uv.size() * 8);
    auto word = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>((value >> shift) & 255));
    };
    word(static_cast<std::uint32_t>(arity));
    word(static_cast<std::uint32_t>(vertexCount));
    word(static_cast<std::uint32_t>(indices.size()));
    word(static_cast<std::uint32_t>(uv.size()));
    for (auto index : indices) word(index);
    for (auto coordinate : uv) {
        word(std::bit_cast<std::uint32_t>(coordinate.u));
        word(std::bit_cast<std::uint32_t>(coordinate.v));
    }
    auto key = bakedAssetContentKey(bytes);
    return Result<DeformTopology>::success(DeformTopology(
        std::make_shared<const Data>(Data{primitive, vertexCount, std::move(indices), std::move(uv), std::move(key)})));
}
DeformPrimitive                DeformTopology::primitive() const noexcept { return data_->primitive; }
std::size_t                    DeformTopology::vertexCount() const noexcept { return data_->vertexCount; }
std::span<const std::uint32_t> DeformTopology::indices() const noexcept { return data_->indices; }
std::span<const DeformUV>      DeformTopology::uv() const noexcept { return data_->uv; }
std::string_view               DeformTopology::key() const noexcept { return data_->key; }
}  // namespace eve::physics
