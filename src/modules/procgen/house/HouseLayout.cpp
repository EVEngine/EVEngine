#include "procgen/house/HouseLayout.h"
#include "procgen/house/HouseComponentLibrary.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "data/ByteData.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "procgen/Semantic.h"

#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/scene.h>
#include <assimp/texture.h>
#include "common/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::housegen {
namespace {
std::string esc(const std::string &v) { std::string o; for (char c : v) { if (c == '\\' || c == '"') o += '\\'; o += c; } return o; }

graphics::Texture *textureFromEmbedded(graphics::Graphics *gfx, const aiTexture *source) {
    if (!source || !source->pcData) return nullptr;
    if (source->mHeight == 0) {
        data::ByteData bytes(source->pcData, static_cast<size_t>(source->mWidth));
        std::unique_ptr<image::ImageData> decoded(image::Image::create()->newImageData(&bytes));
        return gfx->newTexture(decoded.get(), graphics::TextureCreateInfo::withMipmaps(true));
    }
    std::vector<uint8_t> rgba(size_t(source->mWidth) * size_t(source->mHeight) * 4);
    for (size_t pixel = 0; pixel < size_t(source->mWidth) * size_t(source->mHeight); ++pixel) {
        rgba[pixel * 4 + 0] = source->pcData[pixel].r;
        rgba[pixel * 4 + 1] = source->pcData[pixel].g;
        rgba[pixel * 4 + 2] = source->pcData[pixel].b;
        rgba[pixel * 4 + 3] = source->pcData[pixel].a;
    }
    return gfx->newTexture(int(source->mWidth), int(source->mHeight), rgba.data(),
                           graphics::TextureCreateInfo::withMipmaps(true));
}

graphics::Texture *textureFromFile(graphics::Graphics *gfx, const std::string &modelPath,
                                   const std::string &texturePath) {
    if (texturePath.empty()) return nullptr;
    std::filesystem::path resolved(texturePath);
    if (resolved.is_relative()) resolved = std::filesystem::path(modelPath).parent_path() / resolved;
    try {
        if (std::filesystem::is_regular_file(resolved)) {
            std::ifstream input(resolved, std::ios::binary | std::ios::ate);
            const std::streamsize size = input.tellg();
            if (size <= 0) return nullptr;
            input.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            if (!input.read(reinterpret_cast<char *>(bytes.data()), size)) return nullptr;
            data::ByteData source(bytes.data(), bytes.size());
            std::unique_ptr<image::ImageData> decoded(
                image::Image::create()->newImageData(&source));
            return gfx->newTexture(decoded.get(), graphics::TextureCreateInfo::withMipmaps(true));
        }
        return gfx->newTextureFromFile(resolved.lexically_normal().string());
    } catch (...) {
        return nullptr;
    }
}

graphics::Texture *assimpTexture(graphics::Graphics *gfx, const aiScene *scene,
                                 const aiMaterial *material, const std::string &modelPath,
                                 aiTextureType primary, aiTextureType fallback) {
    if (!scene || !material) return nullptr;
    aiString texturePath;
    if (material->GetTexture(primary, 0, &texturePath) != AI_SUCCESS &&
        material->GetTexture(fallback, 0, &texturePath) != AI_SUCCESS)
        return nullptr;
    const std::string path = texturePath.C_Str();
    if (path.empty()) return nullptr;
    if (path.front() == '*') {
        const int index = std::atoi(path.c_str() + 1);
        if (index < 0 || static_cast<unsigned>(index) >= scene->mNumTextures) return nullptr;
        try {
            return textureFromEmbedded(gfx, scene->mTextures[index]);
        } catch (...) {
            return nullptr;
        }
    }
    return textureFromFile(gfx, modelPath, path);
}

eve::ref<model3d::ModelData> loadModel(model3d::Model3D *models, const std::string &path) {
    if (!std::filesystem::is_regular_file(std::filesystem::path(path)))
        return models->newModelDataFromFile(path);  // cache-owned resource
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open model: " + path);
    const std::streamsize size = input.tellg();
    if (size <= 0) throw std::runtime_error("model is empty: " + path);
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(bytes.data()), size))
        throw std::runtime_error("cannot read model: " + path);
    data::ByteData source(bytes.data(), bytes.size());
    return models->newModelData(&source, std::filesystem::path(path).extension().string());
}
}  // namespace

void HouseLayout::clear() {
    instances.clear();
    rooms.clear();
    diagnostics.clear();
    footprintMask.clear();
    seed           = 1;
    moduleSize     = 1.f;
    floorHeight    = 3.f;
    footprintWidth = 0;
    footprintDepth = 0;
    footprintStyle = "rectangle";
    roofStyle      = "gable";
    entranceSide   = "north";
}

std::string HouseLayout::toJson() const {
    std::ostringstream o;
    o << "{\"schema\":\"eve.house-layout\",\"version\":1,\"seed\":" << seed << ",\"moduleSize\":" << moduleSize
      << ",\"floorHeight\":" << floorHeight << ",\"footprintStyle\":\"" << esc(footprintStyle) << "\",\"roofStyle\":\""
      << esc(roofStyle) << "\",\"entranceSide\":\"" << esc(entranceSide) << "\",\"footprintWidth\":" << footprintWidth
      << ",\"footprintDepth\":" << footprintDepth << ",\"footprintMask\":\"";
    for (const uint8_t cell : footprintMask) o << (cell ? '1' : '0');
    o << "\",\"instances\":[";
    for (size_t i = 0; i < instances.size(); ++i) { const auto &v = instances[i]; if (i) o << ','; o << "{\"componentId\":\"" << esc(v.componentId) << "\",\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z << ",\"rotationDeg\":" << v.rotationDeg << '}'; }
    o << "],\"rooms\":[";
    for (size_t i = 0; i < rooms.size(); ++i) { const auto &v = rooms[i]; if (i) o << ','; o << "{\"type\":\"" << esc(v.type) << "\",\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z << ",\"width\":" << v.width << ",\"depth\":" << v.depth << '}'; }
    o << "],\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) { if (i) o << ','; o << '"' << esc(diagnostics[i]) << '"'; }
    o << "]}";
    return o.str();
}

eve::Result<void> HouseLayout::fromJson(std::string_view json) {
    using eve::json::Value;
    std::string               parseError;
    const eve::json::Document doc = eve::json::Document::parse(std::string(json), &parseError);
    if (!doc.valid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, parseError.empty() ? "invalid house layout JSON" : parseError, {}, {},
            "housegen.layout"));
    const Value o = doc.root();
    if (!o.isObject())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "layout must be an object", {}, {}, "housegen.layout"));
    const std::string schema  = o.getString("schema", "eve.house-layout");
    const int         version = o.getInt("version", 1);
    if (schema != "eve.house-layout" || version != 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                                 "unsupported house layout schema or version", "schema",
                                                                 {}, "housegen.layout"));

    HouseLayout parsed;
    parsed.seed = static_cast<unsigned>(o.getInt("seed", 1));
    parsed.moduleSize = o.getFloat("moduleSize", 1.f);
    parsed.floorHeight = o.getFloat("floorHeight", 3.f);
    parsed.footprintStyle = o.getString("footprintStyle", "rectangle");
    parsed.roofStyle = o.getString("roofStyle", "gable");
    parsed.entranceSide = o.getString("entranceSide", "north");
    parsed.footprintWidth           = o.getInt("footprintWidth", 0);
    parsed.footprintDepth           = o.getInt("footprintDepth", 0);
    const std::string footprintBits = o.getString("footprintMask", "");
    if (parsed.footprintWidth < 0 || parsed.footprintDepth < 0 ||
        size_t(parsed.footprintWidth) * size_t(parsed.footprintDepth) != footprintBits.size())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                 "invalid canonical footprint mask", "footprintMask",
                                                                 {}, "housegen.layout"));
    for (const char bit : footprintBits) {
        if (bit != '0' && bit != '1')
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                     "footprint mask must contain only 0 or 1",
                                                                     "footprintMask", {}, "housegen.layout"));
        parsed.footprintMask.push_back(bit == '1' ? 1 : 0);
    }
    if (!std::isfinite(parsed.moduleSize) || parsed.moduleSize <= 0.f || !std::isfinite(parsed.floorHeight) ||
        parsed.floorHeight <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "moduleSize and floorHeight must be finite and positive", {}, {},
            "housegen.layout"));

    const Value instances = o.get("instances");
    if (!instances.isArray())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "layout has no instances", "instances", {}, "housegen.layout"));
    for (size_t i = 0; i < instances.size(); ++i) {
        const Value v = instances.at(i);
        // componentId and the cell coordinates are required, not defaulted.
        if (!v.has("componentId") || !v.has("x") || !v.has("y") || !v.has("z"))
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                     "instance needs componentId, x, y and z",
                                                                     "instances", {}, "housegen.layout"));
        parsed.instances.push_back({v.getString("componentId"), v.getInt("x"), v.getInt("y"),
                                    v.getInt("z"), v.getInt("rotationDeg", 0)});
    }

    const Value rooms = o.get("rooms");
    for (size_t i = 0; i < rooms.size(); ++i) {
        const Value v = rooms.at(i);
        if (!v.has("type") || !v.has("x") || !v.has("y") || !v.has("width") || !v.has("depth"))
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                     "room needs type, x, y, width and depth", "rooms",
                                                                     {}, "housegen.layout"));
        parsed.rooms.push_back({v.getString("type"), v.getInt("x"), v.getInt("y"),
                                v.getInt("z", 0), v.getInt("width"), v.getInt("depth")});
    }

    parsed.diagnostics = o.getStringArray("diagnostics");
    *this = std::move(parsed);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> HouseLayout::validate(const HouseComponentLibrary &library) const {
    if (!std::isfinite(moduleSize) || moduleSize <= 0.f || !std::isfinite(floorHeight) || floorHeight <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "moduleSize and floorHeight must be finite and positive", {}, {},
            "housegen.layout"));
    if (footprintWidth <= 0 || footprintDepth <= 0 ||
        footprintMask.size() != size_t(footprintWidth) * size_t(footprintDepth))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "house layout has no canonical footprint mask",
                                                                 "footprintMask", {}, "housegen.layout"));
    for (const auto& instance : instances)
        if (instance.x >= footprintWidth || instance.y >= footprintDepth ||
            !footprintMask[size_t(instance.y * footprintWidth + instance.x)])
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "house instance origin is outside the canonical footprint",
                instance.componentId, {}, "housegen.layout"));
    for (const HouseRoom& room : rooms) {
        if (room.x < 0 || room.y < 0 || room.width <= 0 || room.depth <= 0 || room.x + room.width > footprintWidth ||
            room.y + room.depth > footprintDepth)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "room lies outside the canonical footprint",
                                                                     "rooms", {}, "housegen.layout"));
        for (int y = room.y; y < room.y + room.depth; ++y)
            for (int x = room.x; x < room.x + room.width; ++x)
                if (!footprintMask[size_t(y * footprintWidth + x)])
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                             "room contains an inactive footprint cell",
                                                                             "rooms", {}, "housegen.layout"));
    }
    std::unordered_set<std::string> occupied;
    std::unordered_set<std::string> floorCells, roofCells;
    std::vector<std::tuple<int, int, int>> floors;
    auto cellKey = [](int x, int y, int z) {
        return std::to_string(x) + ":" + std::to_string(y) + ":" + std::to_string(z);
    };
    bool entrance = false, roof = false;
    for (const auto &i : instances) {
        const auto component = library.find(i.componentId);
        if (!component)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                     "unknown component: " + i.componentId,
                                                                     "componentId", {}, "housegen.layout"));
        const HouseComponent &c = component->get();
        const int             normalizedRotation = (i.rotationDeg % 360 + 360) % 360;
        if (normalizedRotation % 90 != 0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "non-cardinal rotation", "rotationDeg", {}, "housegen.layout"));
        const bool rotationAllowed = std::any_of(c.rotations.begin(), c.rotations.end(), [&](int allowed) {
            return (allowed % 360 + 360) % 360 == normalizedRotation;
        });
        if (!rotationAllowed)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "component does not allow the requested rotation",
                                                                     "rotationDeg", {}, "housegen.layout"));
        const bool quarter = (normalizedRotation / 90) % 2 != 0;
        const int  w = quarter ? c.depth : c.width, d = quarter ? c.width : c.depth;
        for (int y = 0; y < d; ++y) for (int x = 0; x < w; ++x) {
            // Boundary cells legitimately carry two perpendicular wall modules at corners.
            const bool        orientedBoundary = c.category == "wall" || c.category == "door" ||
                                                 c.category == "interior_wall" || c.category == "interior_door";
            const std::string orientation =
                orientedBoundary ? ":" + std::to_string((i.rotationDeg % 360 + 360) % 360) : "";
            const std::string key         = std::to_string(i.x + x) + ":" + std::to_string(i.y + y) + ":" +
                                    std::to_string(i.z) + ":" + c.category + orientation;
            if (!occupied.insert(key).second)
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                         "overlapping " + c.category + " components",
                                                                         {}, {}, "housegen.layout"));
            if (c.category == "floor") {
                floorCells.insert(cellKey(i.x + x, i.y + y, i.z));
                floors.emplace_back(i.x + x, i.y + y, i.z);
            } else if (c.category == "roof") {
                roofCells.insert(cellKey(i.x + x, i.y + y, i.z));
            }
        }
        entrance = entrance || (c.category == "door" && i.z == 0);
        roof     = roof || c.category == "roof";
    }
    if (!entrance)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "house has no entrance", {}, {}, "housegen.layout"));
    if (!roof)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "house has no roof", {}, {}, "housegen.layout"));
    for (const auto &[x, y, z] : floors) {
        if (z > 0 && !floorCells.contains(cellKey(x, y, z - 1))) {
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "upper floor has no structural support", {}, {},
                                                                     "housegen.layout"));
        }
        if (!floorCells.contains(cellKey(x, y, z + 1)) &&
            !roofCells.contains(cellKey(x, y, z + 1))) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "floor cell has no roof coverage", {}, {}, "housegen.layout"));
        }
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> HouseLayout::writeFootprintGrid(procgen::Grid2D &out) const {
    if (!std::isfinite(moduleSize) || moduleSize <= 0.f || !std::isfinite(floorHeight) || floorHeight <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "moduleSize and floorHeight must be finite and positive", {}, {},
            "housegen.layout"));
    for (const auto &instance : instances) {
        if (instance.x < 0 || instance.y < 0 || instance.z < 0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "house instance has a negative grid coordinate",
                instance.componentId, {}, "housegen.layout"));
    }
    if (footprintWidth <= 0 || footprintDepth <= 0 ||
        footprintMask.size() != size_t(footprintWidth) * size_t(footprintDepth))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "house layout has no canonical footprint mask",
                                                                 "footprintMask", {}, "housegen.layout"));

    procgen::Grid2D candidate;
    candidate.resize(footprintWidth, footprintDepth);
    candidate.fill(int(procgen::Semantic::Empty));
    for (int y = 0; y < footprintDepth; ++y)
        for (int x = 0; x < footprintWidth; ++x)
            if (footprintMask[size_t(y * footprintWidth + x)] != 0)
                candidate.setCell(x, y, int(procgen::Semantic::Floor));
    for (const auto &instance : instances) {
        if (instance.x >= footprintWidth || instance.y >= footprintDepth) continue;
        candidate.setDetail(instance.x, instance.y,
                            std::min(255, std::max(candidate.getDetail(instance.x, instance.y), instance.z + 1)));
    }
    candidate.setMeta("domain", "house.footprint");
    candidate.setMeta("seed", std::to_string(seed));
    candidate.setMeta("footprint", footprintStyle);
    candidate.setMeta("roof", roofStyle);
    candidate.setMeta("entrance", entranceSide);
    out = std::move(candidate);
    return eve::Result<void>::success();
}

eve::Result<void> HouseLayout::writeComponentPoints(procgen::PointSet &out) const {
    if (!std::isfinite(moduleSize) || moduleSize <= 0.f || !std::isfinite(floorHeight) || floorHeight <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "moduleSize and floorHeight must be finite and positive", {}, {},
            "housegen.layout"));
    if (footprintWidth <= 0 || footprintDepth <= 0 ||
        footprintMask.size() != size_t(footprintWidth) * size_t(footprintDepth))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "house layout has no canonical footprint mask",
                                                                 "footprintMask", {}, "housegen.layout"));
    for (const auto& instance : instances) {
        const int rotation = (instance.rotationDeg % 360 + 360) % 360;
        if (instance.x < 0 || instance.y < 0 || instance.z < 0 || instance.x >= footprintWidth ||
            instance.y >= footprintDepth || !footprintMask[size_t(instance.y * footprintWidth + instance.x)] ||
            rotation % 90 != 0)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "invalid house component point placement",
                                                                     instance.componentId, {}, "housegen.layout"));
    }
    procgen::PointSet candidate;
    candidate.reserve(instances.size());
    std::uint64_t ordinal = 0;
    const std::uint64_t pointNamespace = (std::uint64_t(seed) << 32u) | 0x484f5553u;
    for (const auto &instance : instances) {
        const int index = candidate.add(float(instance.x) * moduleSize, float(instance.z) * floorHeight,
                                        float(instance.y) * moduleSize);
        candidate.setYaw(index, float(instance.rotationDeg));
        candidate.setPointSeed(index, seed);
        candidate.setBounds(index, 0.f, 0.f, 0.f, moduleSize, floorHeight, moduleSize);
        auto id = candidate.trySetPointId(index, procgen::derivePointId(pointNamespace, ++ordinal));
        if (!id.ok()) return eve::Result<void>::failure(id.status());
        auto component = candidate.trySetStringAttribute(index, "component_id", instance.componentId);
        if (!component.ok()) return eve::Result<void>::failure(component.status());
        auto cellX = candidate.trySetIntAttribute(index, "cell_x", instance.x);
        if (!cellX.ok()) return eve::Result<void>::failure(cellX.status());
        auto cellY = candidate.trySetIntAttribute(index, "cell_y", instance.y);
        if (!cellY.ok()) return eve::Result<void>::failure(cellY.status());
        auto floor = candidate.trySetIntAttribute(index, "floor", instance.z);
        if (!floor.ok()) return eve::Result<void>::failure(floor.status());
    }
    out = std::move(candidate);
    return eve::Result<void>::success();
}

eve::Result<std::vector<ecs::EntityHandle>> HouseLayout::instantiate(graphics::Graphics &gfx, model3d::Model3D &models,
                                                                     const HouseComponentLibrary &library) const {
    const auto valid = validate(library);
    if (!valid.ok()) return eve::Result<std::vector<ecs::EntityHandle>>::failure(valid.status());
    std::vector<ecs::EntityHandle> entities;
    const auto                     destroyCreated = [&entities]() noexcept {
        for (const auto &handle : entities) {
            if (auto *entity = ecs::try_get(handle)) ecs::DestroyEntity(entity);
        }
        entities.clear();
    };
    std::unordered_map<std::string, eve::ref<model3d::ModelData>> data;
    struct CachedPart {
        graphics::Mesh *mesh = nullptr;
        graphics::Texture *texture = nullptr;
        graphics::Texture *normalTexture = nullptr;
        graphics::Texture *heightTexture = nullptr;
        float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
        float metallic = 0.f, roughness = 0.72f;
        float parallaxScale = 0.f, parallaxMinLayers = 8.f, parallaxMaxLayers = 32.f;
        float cellBombScale = 4.f, cellBombStrength = 0.f, cellBombRotation = 1.f;
    };
    std::unordered_map<std::string, std::vector<CachedPart>> parts;
    try {
        for (const auto &i : instances) {
            const auto component = library.find(i.componentId);
            if (!component) {
                destroyCreated();
                return eve::Result<std::vector<ecs::EntityHandle>>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "unknown component: " + i.componentId,
                                           "componentId", {}, "housegen.layout"));
            }
            const HouseComponent &c = component->get();
            // eve::ref cannot represent null, so look up before default-inserting.
            auto modelIt = data.find(c.modelPath);
            if (modelIt == data.end()) modelIt = data.emplace(c.modelPath, loadModel(&models, c.modelPath)).first;
            model3d::ModelData *model = modelIt->second.get();
            // Material overrides belong to a component, so two components may safely reuse the
            // same GLB with different architectural finishes.
            auto &cached = parts[c.id];
            if (cached.empty()) {
                const aiScene *scene = model->getScene();
                if (!scene || !scene->mRootNode) throw std::runtime_error("model has no scene root: " + c.modelPath);
                std::function<void(const aiNode *, const aiMatrix4x4 &)> walk =
                    [&](const aiNode *node, const aiMatrix4x4 &parent) {
                        const aiMatrix4x4 world = parent * node->mTransformation;
                        for (unsigned ni = 0; ni < node->mNumMeshes; ++ni) {
                            const unsigned mi = node->mMeshes[ni];
                            if (mi >= scene->mNumMeshes || !scene->mMeshes[mi]) continue;
                            const aiMesh *source = scene->mMeshes[mi];
                            CachedPart part;
                            part.mesh = gfx.newMeshFromAssimp(*source, world);
                            if (scene->mMaterials && source->mMaterialIndex < scene->mNumMaterials) {
                                const aiMaterial *material = scene->mMaterials[source->mMaterialIndex];
                                aiColor4D color(1.f, 1.f, 1.f, 1.f);
                                material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
                                material->Get(AI_MATKEY_BASE_COLOR, color);
                                part.r = color.r; part.g = color.g; part.b = color.b; part.a = color.a;
                                material->Get(AI_MATKEY_METALLIC_FACTOR, part.metallic);
                                material->Get(AI_MATKEY_ROUGHNESS_FACTOR, part.roughness);
                                part.texture       = assimpTexture(&gfx, scene, material, c.modelPath,
                                                                   aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE);
                                part.normalTexture = assimpTexture(&gfx, scene, material, c.modelPath,
                                                                   aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA);
                                part.heightTexture = assimpTexture(&gfx, scene, material, c.modelPath,
                                                                   aiTextureType_HEIGHT, aiTextureType_DISPLACEMENT);
                            }
                            if (c.material.hasBaseColor) {
                                part.r = c.material.baseColorR;
                                part.g = c.material.baseColorG;
                                part.b = c.material.baseColorB;
                                part.a = c.material.baseColorA;
                            }
                            if (!c.material.baseColorTexture.empty())
                                part.texture = textureFromFile(&gfx, c.modelPath, c.material.baseColorTexture);
                            if (!c.material.normalTexture.empty())
                                part.normalTexture = textureFromFile(&gfx, c.modelPath, c.material.normalTexture);
                            if (!c.material.heightTexture.empty())
                                part.heightTexture = textureFromFile(&gfx, c.modelPath, c.material.heightTexture);
                            if (c.material.hasMetallic) part.metallic = c.material.metallic;
                            if (c.material.hasRoughness) part.roughness = c.material.roughness;
                            part.parallaxScale     = c.material.parallaxScale;
                            part.parallaxMinLayers = c.material.parallaxMinLayers;
                            part.parallaxMaxLayers = c.material.parallaxMaxLayers;
                            part.cellBombScale     = c.material.cellBombScale;
                            part.cellBombStrength  = c.material.cellBombStrength;
                            part.cellBombRotation  = c.material.cellBombRotation;
                            cached.push_back(part);
                        }
                        for (unsigned child = 0; child < node->mNumChildren; ++child)
                            walk(node->mChildren[child], world);
                    };
                walk(scene->mRootNode, aiMatrix4x4());
            }
            for (const auto &part : cached) {
                auto *e = graphics::Renderable3D::create();
                if (!e) {
                    destroyCreated();
                    return eve::Result<std::vector<ecs::EntityHandle>>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "failed to create Renderable3D entity", {},
                                               {}, "housegen.layout"));
                }
                e->setMesh(part.mesh);
                e->setPosition(i.x * moduleSize, i.z * floorHeight, i.y * moduleSize);
                e->setYaw(float(i.rotationDeg) * 3.14159265358979323846f / 180.f);
                e->setTint(part.r, part.g, part.b, part.a);
                if (part.texture) e->setTexture(part.texture);
                if (part.normalTexture) e->setNormalTexture(part.normalTexture);
                if (part.heightTexture) e->setHeightTexture(part.heightTexture);
                e->setMetallic(part.metallic);
                e->setRoughness(part.roughness);
                e->setTexCellBomb(part.cellBombScale, part.cellBombStrength,
                                  part.cellBombRotation);
                if (part.heightTexture && part.parallaxScale > 0.f)
                    e->setParallax(part.parallaxScale, part.parallaxMinLayers,
                                   part.parallaxMaxLayers);
                entities.push_back(ecs::handle_of(e));
            }
        }
    } catch (const std::exception &e) {
        destroyCreated();
        return eve::Result<std::vector<ecs::EntityHandle>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, e.what(), {}, {}, "housegen.layout"));
    } catch (...) {
        destroyCreated();
        return eve::Result<std::vector<ecs::EntityHandle>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "house layout instantiation failed", {}, {}, "housegen.layout"));
    }
    return eve::Result<std::vector<ecs::EntityHandle>>::success(std::move(entities),
                                                                eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::housegen
