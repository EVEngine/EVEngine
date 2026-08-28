#include "housegen/HousePersistence.h"

#include "housegen/HouseLayout.h"

#include "procgen/GeneratedArtifact.h"
#include "procgen/Grid2D.h"

#include "common/Value.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace eve::housegen {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "housegen.persistence"));
}

std::uint64_t fnv1a(std::string_view text) noexcept {
    std::uint64_t value = 14695981039346656037ull;
    for (const unsigned char byte : text) {
        value ^= byte;
        value *= 1099511628211ull;
    }
    return value;
}

/** @brief Derive a deterministic, stable, non-nil artifact id from build-key text. */
eve::ArtifactId artifactIdFromText(const std::string &text) noexcept {
    std::array<std::uint8_t, 16> bytes{};
    const std::uint64_t          lo = fnv1a(text);
    const std::uint64_t          hi = fnv1a(text + "/housegen");
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[i]     = static_cast<std::uint8_t>((hi >> (i * 8u)) & 0xffu);
        bytes[8 + i] = static_cast<std::uint8_t>((lo >> (i * 8u)) & 0xffu);
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fu) | 0x40u);  // UUID v4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fu) | 0x80u);  // RFC 4122 variant
    const auto parsed = eve::ArtifactId::fromBytes(bytes);
    return parsed ? *parsed : eve::ArtifactId::nil();
}

std::string join(const std::vector<std::string> &values, char sep) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out.push_back(sep);
        out += values[i];
    }
    return out;
}

/** @brief Cell-extent bounds over every placed instance (tile coordinates). */
procgen::Bounds layoutBounds(const HouseLayout &layout) noexcept {
    procgen::Bounds bounds;
    if (layout.instances.empty()) return bounds;
    int minX = layout.instances.front().x, minY = layout.instances.front().y;
    int minZ = layout.instances.front().z, maxX = minX, maxY = minY, maxZ = minZ;
    for (const auto &instance : layout.instances) {
        minX = std::min(minX, instance.x);
        minY = std::min(minY, instance.y);
        minZ = std::min(minZ, instance.z);
        maxX = std::max(maxX, instance.x);
        maxY = std::max(maxY, instance.y);
        maxZ = std::max(maxZ, instance.z);
    }
    bounds.minX = float(minX);
    bounds.minY = float(minY);
    bounds.minZ = float(minZ);
    bounds.maxX = float(maxX + 1);
    bounds.maxY = float(maxY + 1);
    bounds.maxZ = float(maxZ + 1);
    bounds.valid = true;
    return bounds;
}

}  // namespace

std::string houseRequestBuildKeyText(const HouseRequest &request) {
    std::ostringstream out;
    out << "housegen/request;seed=" << request.seed << ";size=" << request.width << "x" << request.depth
        << ";floors=" << request.floors << ";moduleSize=" << request.moduleSize
        << ";floorHeight=" << request.floorHeight << ";style=" << request.style << ";footprint="
        << request.footprint << ";roof=" << request.roof << ";entrance=" << request.entrance
        << ";requiredRooms=" << join(request.requiredRooms, ',');
    out << ";perimeter=";
    for (size_t i = 0; i < request.perimeter.size(); ++i) {
        if (i) out << '|';
        out << request.perimeter[i].x << ',' << request.perimeter[i].y;
    }
    return out.str();
}

std::string houseLayoutBuildKeyText(const HouseLayout &layout) { return "housegen/layout;" + layout.toJson(); }

eve::Result<eve::ArtifactId> publishHouseLayout(procgen::ArtifactStore &store, const HouseLayout &layout) {
    const std::string canonical = houseLayoutBuildKeyText(layout);
    const auto        key       = procgen::BuildKey::fromCanonical(canonical);
    if (!key)
        return failure<eve::ArtifactId>(eve::DiagnosticCode::InvalidArgument,
                                        "house layout cannot form a deterministic build key");
    const eve::ArtifactId id = artifactIdFromText(canonical);
    if (id.isNil())
        return failure<eve::ArtifactId>(eve::DiagnosticCode::Failed, "house layout artifact identity is nil");

    procgen::Grid2D footprint;
    const int       width  = std::max(1, static_cast<int>(layoutBounds(layout).maxX));
    const int       depth  = std::max(1, static_cast<int>(layoutBounds(layout).maxY));
    footprint.resize(width, depth);
    footprint.fill(0);
    for (const auto &instance : layout.instances) {
        if (instance.x >= 0 && instance.y >= 0 && instance.x < width && instance.y < depth)
            footprint.setCell(instance.x, instance.y, 1);
    }

    eve::Value::Object metadata;
    metadata.emplace("schema", eve::Value("housegen.layout"));
    metadata.emplace("layout", eve::Value(layout.toJson()));

    auto artifact = procgen::makeArtifact(id, procgen::ArtifactType::Grid, eve::SchemaVersion(1), *key,
                                          layoutBounds(layout), {}, std::move(metadata),
                                          procgen::GeneratedArtifact::Payload(std::move(footprint)));
    if (!artifact.ok()) return eve::Result<eve::ArtifactId>::failure(artifact.status());
    auto published = store.publish(std::move(artifact).takeValue());
    if (!published.ok()) return eve::Result<eve::ArtifactId>::failure(published.status());
    return eve::Result<eve::ArtifactId>::success(std::move(published).takeValue(),
                                                 eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<HouseLayout> restoreHouseLayout(const procgen::GeneratedArtifact &artifact) {
    if (artifact.type != procgen::ArtifactType::Grid)
        return failure<HouseLayout>(eve::DiagnosticCode::InvalidArgument, "house layout artifact is not a grid",
                                    "artifact.type");
    const auto layoutIt = artifact.metadata.find("layout");
    if (layoutIt == artifact.metadata.end() || !layoutIt->second.isString())
        return failure<HouseLayout>(eve::DiagnosticCode::ParseError, "house layout artifact has no layout payload",
                                    "artifact.metadata.layout");
    HouseLayout layout;
    auto        restored = layout.fromJson(layoutIt->second.asString());
    if (!restored.ok()) return eve::Result<HouseLayout>::failure(restored.status());
    return eve::Result<HouseLayout>::success(std::move(layout),
                                             eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::housegen