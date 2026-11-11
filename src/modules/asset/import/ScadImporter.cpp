#include "asset/import/StructuredModelCommon.h"
#include "asset/import/ImportCommon.h"
#include "common/StringMapInstances.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace eve::asset_import {
namespace {
Result<structured_detail::MeshPart> decodeBinaryStl(const std::vector<std::uint8_t>& bytes,
                                                    const AssetImportLimits&         limits) {
    using Part   = Result<structured_detail::MeshPart>;
    auto invalid = [](std::string message) {
        return Part::failure(
            Diagnostic::error(DiagnosticCode::ParseError, std::move(message), {}, {}, "asset.import.scad"));
    };
    if (bytes.size() < 84 || bytes.size() > limits.maximumSourceBytes)
        return invalid("binary STL is missing or outside budget");
    auto word = [&](std::size_t offset) {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
               (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    const auto count = word(80);
    if (count == 0 || 84ull + std::uint64_t(count) * 50 != bytes.size() ||
        std::uint64_t(count) * 3 > limits.maximumVerticesPerPrimitive ||
        std::uint64_t(count) * 3 > limits.maximumIndicesPerPrimitive ||
        std::uint64_t(count) * 84 + 24 > limits.maximumDecodedBytes)
        return invalid("binary STL triangle count/length/budget is invalid");
    structured_detail::MeshPart part;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto offset = std::size_t(84) + std::size_t(i) * 50;
        if (bytes[offset + 48] != 0 || bytes[offset + 49] != 0)
            return invalid("coloured/extended STL is unsupported; export plain binary STL");
        for (std::size_t j = 0; j < 3; ++j)
            if (!std::isfinite(std::bit_cast<float>(word(offset + j * 4)))) return invalid("nonfinite STL normal");
        std::array<std::array<double, 3>, 3> triangle{};
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            const auto   base = offset + 12 + vertex * 12;
            const double x    = std::bit_cast<float>(word(base));
            const double y    = std::bit_cast<float>(word(base + 4));
            const double z    = std::bit_cast<float>(word(base + 8));
            triangle[vertex]  = {x * 0.001, z * 0.001, -y * 0.001};
        }
        auto added = structured_detail::appendTriangle(part.mesh, triangle, limits);
        if (!added) return Part::failure(added.status());
    }
    return Part::success(std::move(part));
}

double numeric(const Value& value) {
    if (value.isInt64()) return static_cast<double>(value.asInt());
    return value.asDouble();
}

Result<Value::Object> parseScadRig(const std::vector<std::uint8_t>& bytes, const AssetImportLimits& limits,
                                   const std::set<std::string>* requiredPartNames) {
    using Output = Result<Value::Object>;
    auto invalid = [](std::string message) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::ParseError, std::move(message), {}, {}, "asset.import.scad"));
    };
    if (bytes.empty() || bytes.size() > limits.maximumSourceBytes)
        return invalid("ScadRig document is missing or outside budget");
    auto parsed = Value::fromJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!parsed) return Output::failure(parsed.status());
    const auto* root = parsed.value().getIf<Value::Object>();
    if (!root) return invalid("ScadRig root must be a JSON object");
    const auto* schema  = parsed.value().find("schema");
    const auto* version = parsed.value().find("schemaVersion");
    const auto* unknown = parsed.value().find("unknownFields");
    if (!schema || !schema->isString() || schema->asString() != "eve.scad-rig" || !version || !version->isInt64() ||
        version->asInt() != 1 || !unknown || !unknown->isString() ||
        (unknown->asString() != "reject" && unknown->asString() != "preserve"))
        return invalid("ScadRig requires schema eve.scad-rig/1 and unknownFields reject|preserve");
    if (unknown->asString() == "reject") {
        static const std::unordered_set<std::string> known = {"schema",         "schemaVersion", "unknownFields",
                                                              "coordinateSystem", "joints",         "parts"};
        for (const auto& key : parsed.value().keys())
            if (!known.count(key)) return invalid("ScadRig unknown field rejected: " + key);
    }
    const auto* coordinate = parsed.value().find("coordinateSystem");
    if (coordinate && (!coordinate->isString() || coordinate->asString() != "openscad-mm-z-up"))
        return invalid("ScadRig coordinateSystem must be openscad-mm-z-up when present");
    const auto* jointsValue = parsed.value().find("joints");
    const auto* partsValue  = parsed.value().find("parts");
    if (!jointsValue || !jointsValue->isArray() || jointsValue->arraySize() == 0 || !partsValue ||
        !partsValue->isArray() || partsValue->arraySize() == 0)
        return invalid("ScadRig requires non-empty joints and parts arrays");
    if (jointsValue->arraySize() > limits.maximumAssets || partsValue->arraySize() > limits.maximumAssets)
        return invalid("ScadRig joint/part count exceeds budget");

    std::unordered_map<std::string, std::string> parents;
    std::unordered_set<std::string>              jointNames;
    std::size_t                                  roots = 0;
    for (std::size_t i = 0; i < jointsValue->arraySize(); ++i) {
        const auto& jointValue = jointsValue->at(i);
        if (!jointValue.isObject()) return invalid("ScadRig joint entries must be objects");
        if (unknown->asString() == "reject") {
            static const std::unordered_set<std::string> known = {"name", "parent", "pivot", "axis", "limits"};
            for (const auto& key : jointValue.keys())
                if (!known.count(key)) return invalid("ScadRig joint unknown field rejected: " + key);
        }
        const auto* name = jointValue.find("name");
        if (!name || !name->isString() || name->asString().empty() ||
            name->asString().size() > limits.maximumStringBytes)
            return invalid("ScadRig joint name is required and bounded");
        if (!jointNames.insert(name->asString()).second) return invalid("ScadRig joint names must be unique");
        const auto* parent = jointValue.find("parent");
        if (!parent || parent->isNull()) {
            parents.emplace(name->asString(), std::string{});
            ++roots;
        } else if (parent->isString() && !parent->asString().empty()) {
            parents.emplace(name->asString(), parent->asString());
        } else {
            return invalid("ScadRig joint parent must be null or a non-empty string");
        }
        const auto* pivot = jointValue.find("pivot");
        const auto* axis  = jointValue.find("axis");
        if (!pivot || !pivot->isArray() || pivot->arraySize() != 3 || !axis || !axis->isArray() ||
            axis->arraySize() != 3)
            return invalid("ScadRig joint pivot/axis must be length-3 numeric arrays");
        for (std::size_t axisIndex = 0; axisIndex < 3; ++axisIndex) {
            if (!pivot->at(axisIndex).isNumeric() || !std::isfinite(numeric(pivot->at(axisIndex))) ||
                !axis->at(axisIndex).isNumeric() || !std::isfinite(numeric(axis->at(axisIndex))))
                return invalid("ScadRig joint pivot/axis values must be finite numbers");
        }
        const double ax = numeric(axis->at(0)), ay = numeric(axis->at(1)), az = numeric(axis->at(2));
        if (ax * ax + ay * ay + az * az <= 0) return invalid("ScadRig joint axis must be non-zero");
        if (const auto* limitsValue = jointValue.find("limits")) {
            if (!limitsValue->isArray() || limitsValue->arraySize() != 2 || !limitsValue->at(0).isNumeric() ||
                !limitsValue->at(1).isNumeric() || !std::isfinite(numeric(limitsValue->at(0))) ||
                !std::isfinite(numeric(limitsValue->at(1))) ||
                numeric(limitsValue->at(0)) > numeric(limitsValue->at(1)))
                return invalid("ScadRig joint limits must be [min,max] degrees with min <= max");
        }
    }
    if (roots != 1) return invalid("ScadRig requires exactly one root joint (parent null)");
    for (const auto& [name, parent] : parents) {
        if (parent.empty()) continue;
        if (!jointNames.count(parent)) return invalid("ScadRig joint parent is unknown: " + parent);
        std::unordered_set<std::string> seen{name};
        auto                            cursor = parent;
        while (!cursor.empty()) {
            if (!seen.insert(cursor).second) return invalid("ScadRig joint parent graph contains a cycle");
            cursor = parents[cursor];
        }
    }

    std::set<std::string> partNames;
    for (std::size_t i = 0; i < partsValue->arraySize(); ++i) {
        const auto& partValue = partsValue->at(i);
        if (!partValue.isObject()) return invalid("ScadRig part entries must be objects");
        if (unknown->asString() == "reject") {
            static const std::unordered_set<std::string> known = {"name", "joint"};
            for (const auto& key : partValue.keys())
                if (!known.count(key)) return invalid("ScadRig part unknown field rejected: " + key);
        }
        const auto* name  = partValue.find("name");
        const auto* joint = partValue.find("joint");
        if (!name || !name->isString() || name->asString().empty() ||
            name->asString().size() > limits.maximumStringBytes || !joint || !joint->isString() ||
            !jointNames.count(joint->asString()))
            return invalid("ScadRig part name/joint binding is invalid");
        if (!partNames.insert(name->asString()).second) return invalid("ScadRig part names must be unique");
    }
    if (requiredPartNames && *requiredPartNames != partNames)
        return invalid("ScadRig parts must match compiled mesh part names exactly");
    return Output::success(*root);
}

Result<void> attachScadRig(PreparedAssetImport& prepared, const ImportPackageIdentity& identity,
                           const Value::Object& rig) {
    using Output     = Result<void>;
    const auto rigId = identity.packageId.child("scad:rig");
    auto       reference = detail::assetRef(rigId);
    if (!reference) return Output::failure(reference.status());
    auto json = Value(rig).toJson();
    if (!json) return Output::failure(json.status());
    const auto                path = "assets/" + rigId.format() + "/asset.json";
    std::vector<std::uint8_t> definition(json.value().begin(), json.value().end());
    prepared.manifest.assets.push_back({reference.value(),
                                        "eve.scad-rig",
                                        SchemaVersion(1),
                                        path,
                                        detail::sha256(definition),
                                        {"source:scad"}});
    prepared.entries.push_back({path, std::move(definition)});
    prepared.manifest.entrypoints.emplace("rig", reference.value());
    prepared.manifest.provenance["scadRig"] = Value(reference.value().format());
    prepared.sourceMappings.push_back({"scad-rig", reference.value()});
    return Output::success();
}
}  // namespace

Result<PreparedAssetImport> prepareScadImport(const ScadImportRequest& request) {
    using Output = Result<PreparedAssetImport>;
    auto invalid = [](std::string message) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::ParseError, std::move(message), {}, {}, "asset.import.scad"));
    };
    if (request.sourceBytes.empty() || request.sourceBytes.size() > request.limits.maximumSourceBytes)
        return invalid("SCAD source is missing or outside budget");

    std::map<std::string, std::vector<std::uint8_t>> stls = request.partStls;
    if (stls.empty()) {
        if (request.binaryStl.empty()) return invalid("SCAD binary STL is missing");
        stls.emplace("default", request.binaryStl);
    } else if (!request.binaryStl.empty()) {
        return invalid("provide either binaryStl or partStls, not both");
    }
    if (stls.size() > request.limits.maximumAssets) return invalid("SCAD part count exceeds budget");

    std::map<std::string, structured_detail::MeshPart> parts;
    std::set<std::string>                              partNames;
    for (const auto& [name, bytes] : stls) {
        if (name.empty() || name.size() > request.limits.maximumStringBytes) return invalid("SCAD part name is invalid");
        if (!partNames.insert(name).second) return invalid("SCAD part names must be unique");
        auto decoded = decodeBinaryStl(bytes, request.limits);
        if (!decoded) return Output::failure(decoded.status());
        parts.emplace(name, std::move(decoded).takeValue());
    }

    std::vector<ImportFinding> findings{
        {request.sourceName, "SCAD.geometry", ImportDisposition::Baked,
         "OpenSCAD binary STL triangulated; millimetres/Z-up converted to metres/Y-up"},
        {request.sourceName, "SCAD.nonGeometry", ImportDisposition::Unsupported,
         "STL does not retain colours or animation; root source retained, external dependency closure not archived"}};

    std::optional<Value::Object> rig;
    if (!request.rigBytes.empty()) {
        const bool multiPart = !(partNames.size() == 1 && partNames.count("default"));
        auto       parsed    = parseScadRig(request.rigBytes, request.limits, multiPart ? &partNames : nullptr);
        if (!parsed) return Output::failure(parsed.status());
        rig = std::move(parsed).takeValue();
        findings.push_back({request.sourceName, "SCAD.rig", ImportDisposition::Translated,
                            "eve.scad-rig/1 joint tree retained as metadata; static prefab pose stays identity"});
    }

    std::map<std::string, std::vector<std::uint8_t>> sources{{request.sourceName, request.sourceBytes}};
    if (!request.rigBytes.empty()) sources.emplace("scad-rig.json", request.rigBytes);

    auto prepared = structured_detail::packageModel(request.package, request.sourceName, "scad", std::move(parts),
                                                    sources, std::move(findings), request.limits);
    if (!prepared) return prepared;
    if (rig) {
        prepared.value().entries.erase(
            std::remove_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                           [](const asset::EvaArchiveEntry& entry) { return entry.path == "reports/import.json"; }),
            prepared.value().entries.end());
        auto attached = attachScadRig(prepared.value(), request.package, *rig);
        if (!attached) return Output::failure(attached.status());
        auto report = detail::finalizeImportReport(prepared.value(), request.package, "scad", "1");
        if (!report) return Output::failure(report.status());
    }
    return prepared;
}
}  // namespace eve::asset_import
