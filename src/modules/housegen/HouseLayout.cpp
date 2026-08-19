#include "housegen/HouseLayout.h"
#include "housegen/HouseComponentLibrary.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "data/ByteData.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/scene.h>
#include <assimp/texture.h>
#include "common/Json.h"

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

std::unique_ptr<model3d::ModelData> loadModel(model3d::Model3D *models,
                                              const std::string &path) {
    if (!std::filesystem::is_regular_file(std::filesystem::path(path)))
        return std::unique_ptr<model3d::ModelData>(models->newModelDataFromFile(path));
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open model: " + path);
    const std::streamsize size = input.tellg();
    if (size <= 0) throw std::runtime_error("model is empty: " + path);
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(bytes.data()), size))
        throw std::runtime_error("cannot read model: " + path);
    data::ByteData source(bytes.data(), bytes.size());
    return std::unique_ptr<model3d::ModelData>(
        models->newModelData(&source, std::filesystem::path(path).extension().string()));
}
}

void HouseLayout::clear() { instances.clear(); rooms.clear(); diagnostics.clear(); seed = 1; moduleSize = 1.f; floorHeight = 3.f; footprintStyle = "rectangle"; roofStyle = "gable"; entranceSide = "north"; }

std::string HouseLayout::toJson() const {
    std::ostringstream o;
    o << "{\"seed\":" << seed << ",\"moduleSize\":" << moduleSize << ",\"floorHeight\":" << floorHeight
      << ",\"footprintStyle\":\"" << esc(footprintStyle) << "\",\"roofStyle\":\"" << esc(roofStyle)
      << "\",\"entranceSide\":\"" << esc(entranceSide) << "\",\"instances\":[";
    for (size_t i = 0; i < instances.size(); ++i) { const auto &v = instances[i]; if (i) o << ','; o << "{\"componentId\":\"" << esc(v.componentId) << "\",\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z << ",\"rotationDeg\":" << v.rotationDeg << '}'; }
    o << "],\"rooms\":[";
    for (size_t i = 0; i < rooms.size(); ++i) { const auto &v = rooms[i]; if (i) o << ','; o << "{\"type\":\"" << esc(v.type) << "\",\"x\":" << v.x << ",\"y\":" << v.y << ",\"width\":" << v.width << ",\"depth\":" << v.depth << '}'; }
    o << "],\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) { if (i) o << ','; o << '"' << esc(diagnostics[i]) << '"'; }
    o << "]}";
    return o.str();
}

bool HouseLayout::fromJson(const std::string &json, std::string *error) {
    using eve::json::Value;
    const eve::json::Document doc = eve::json::Document::parse(json, error);
    if (!doc.valid()) return false;
    const Value o = doc.root();
    if (!o.isObject()) { if (error) *error = "layout must be an object"; return false; }

    HouseLayout parsed;
    parsed.seed = static_cast<unsigned>(o.getInt("seed", 1));
    parsed.moduleSize = o.getFloat("moduleSize", 1.f);
    parsed.floorHeight = o.getFloat("floorHeight", 3.f);
    parsed.footprintStyle = o.getString("footprintStyle", "rectangle");
    parsed.roofStyle = o.getString("roofStyle", "gable");
    parsed.entranceSide = o.getString("entranceSide", "north");

    const Value instances = o.get("instances");
    if (!instances.isArray()) { if (error) *error = "layout has no instances"; return false; }
    for (size_t i = 0; i < instances.size(); ++i) {
        const Value v = instances.at(i);
        // componentId and the cell coordinates are required, not defaulted.
        if (!v.has("componentId") || !v.has("x") || !v.has("y") || !v.has("z")) {
            if (error) *error = "instance needs componentId, x, y and z";
            return false;
        }
        parsed.instances.push_back({v.getString("componentId"), v.getInt("x"), v.getInt("y"),
                                    v.getInt("z"), v.getInt("rotationDeg", 0)});
    }

    const Value rooms = o.get("rooms");
    for (size_t i = 0; i < rooms.size(); ++i) {
        const Value v = rooms.at(i);
        if (!v.has("type") || !v.has("x") || !v.has("y") || !v.has("width") || !v.has("depth")) {
            if (error) *error = "room needs type, x, y, width and depth";
            return false;
        }
        parsed.rooms.push_back({v.getString("type"), v.getInt("x"), v.getInt("y"),
                                v.getInt("width"), v.getInt("depth")});
    }

    parsed.diagnostics = o.getStringArray("diagnostics");
    *this = std::move(parsed);
    return true;
}

bool HouseLayout::validate(const HouseComponentLibrary &library, std::string *error) const {
    std::unordered_set<std::string> occupied;
    std::unordered_set<std::string> floorCells, roofCells;
    std::vector<std::tuple<int, int, int>> floors;
    auto cellKey = [](int x, int y, int z) {
        return std::to_string(x) + ":" + std::to_string(y) + ":" + std::to_string(z);
    };
    bool entrance = false, roof = false;
    for (const auto &i : instances) {
        const auto *c = library.find(i.componentId);
        if (!c) { if (error) *error = "unknown component: " + i.componentId; return false; }
        if (i.rotationDeg % 90 != 0) { if (error) *error = "non-cardinal rotation"; return false; }
        const bool quarter = (i.rotationDeg / 90) % 2 != 0;
        const int w = quarter ? c->depth : c->width, d = quarter ? c->width : c->depth;
        for (int y = 0; y < d; ++y) for (int x = 0; x < w; ++x) {
            // Boundary cells legitimately carry two perpendicular wall modules at corners.
            const std::string orientation = (c->category == "wall" || c->category == "door")
                                                ? ":" + std::to_string((i.rotationDeg % 360 + 360) % 360)
                                                : "";
            const std::string key = std::to_string(i.x + x) + ":" + std::to_string(i.y + y) + ":" + std::to_string(i.z) + ":" + c->category + orientation;
            if (!occupied.insert(key).second) { if (error) *error = "overlapping " + c->category + " components"; return false; }
            if (c->category == "floor") {
                floorCells.insert(cellKey(i.x + x, i.y + y, i.z));
                floors.emplace_back(i.x + x, i.y + y, i.z);
            } else if (c->category == "roof") {
                roofCells.insert(cellKey(i.x + x, i.y + y, i.z));
            }
        }
        entrance = entrance || (c->category == "door" && i.z == 0);
        roof = roof || c->category == "roof";
    }
    if (!entrance) { if (error) *error = "house has no entrance"; return false; }
    if (!roof) { if (error) *error = "house has no roof"; return false; }
    for (const auto &[x, y, z] : floors) {
        if (z > 0 && !floorCells.contains(cellKey(x, y, z - 1))) {
            if (error) *error = "upper floor has no structural support";
            return false;
        }
        if (!floorCells.contains(cellKey(x, y, z + 1)) &&
            !roofCells.contains(cellKey(x, y, z + 1))) {
            if (error) *error = "floor cell has no roof coverage";
            return false;
        }
    }
    return true;
}

std::vector<graphics::Renderable3D *> HouseLayout::instantiate(graphics::Graphics *gfx, model3d::Model3D *models, const HouseComponentLibrary &library, std::string *error) const {
    std::vector<graphics::Renderable3D *> entities;
    if (!gfx || !models) { if (error) *error = "graphics and model3d are required"; return entities; }
    std::unordered_map<std::string, std::unique_ptr<model3d::ModelData>> data;
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
            const auto *c = library.find(i.componentId); if (!c) continue;
            auto &model = data[c->modelPath];
            if (!model) model = loadModel(models, c->modelPath);
            // Material overrides belong to a component, so two components may safely reuse the
            // same GLB with different architectural finishes.
            auto &cached = parts[c->id];
            if (cached.empty()) {
                const aiScene *scene = model->getScene();
                if (!scene || !scene->mRootNode) throw std::runtime_error("model has no scene root: " + c->modelPath);
                std::function<void(const aiNode *, const aiMatrix4x4 &)> walk =
                    [&](const aiNode *node, const aiMatrix4x4 &parent) {
                        const aiMatrix4x4 world = parent * node->mTransformation;
                        for (unsigned ni = 0; ni < node->mNumMeshes; ++ni) {
                            const unsigned mi = node->mMeshes[ni];
                            if (mi >= scene->mNumMeshes || !scene->mMeshes[mi]) continue;
                            const aiMesh *source = scene->mMeshes[mi];
                            CachedPart part;
                            part.mesh = gfx->newMeshFromAssimp(*source, world);
                            if (scene->mMaterials && source->mMaterialIndex < scene->mNumMaterials) {
                                const aiMaterial *material = scene->mMaterials[source->mMaterialIndex];
                                aiColor4D color(1.f, 1.f, 1.f, 1.f);
                                material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
                                material->Get(AI_MATKEY_BASE_COLOR, color);
                                part.r = color.r; part.g = color.g; part.b = color.b; part.a = color.a;
                                material->Get(AI_MATKEY_METALLIC_FACTOR, part.metallic);
                                material->Get(AI_MATKEY_ROUGHNESS_FACTOR, part.roughness);
                                part.texture = assimpTexture(gfx, scene, material, c->modelPath,
                                                             aiTextureType_BASE_COLOR,
                                                             aiTextureType_DIFFUSE);
                                part.normalTexture = assimpTexture(gfx, scene, material, c->modelPath,
                                                                   aiTextureType_NORMALS,
                                                                   aiTextureType_NORMAL_CAMERA);
                                part.heightTexture = assimpTexture(gfx, scene, material, c->modelPath,
                                                                   aiTextureType_HEIGHT,
                                                                   aiTextureType_DISPLACEMENT);
                            }
                            if (c->material.hasBaseColor) {
                                part.r = c->material.baseColorR; part.g = c->material.baseColorG;
                                part.b = c->material.baseColorB; part.a = c->material.baseColorA;
                            }
                            if (!c->material.baseColorTexture.empty())
                                part.texture = textureFromFile(gfx, c->modelPath,
                                                               c->material.baseColorTexture);
                            if (!c->material.normalTexture.empty())
                                part.normalTexture = textureFromFile(gfx, c->modelPath,
                                                                     c->material.normalTexture);
                            if (!c->material.heightTexture.empty())
                                part.heightTexture = textureFromFile(gfx, c->modelPath,
                                                                     c->material.heightTexture);
                            if (c->material.hasMetallic) part.metallic = c->material.metallic;
                            if (c->material.hasRoughness) part.roughness = c->material.roughness;
                            part.parallaxScale = c->material.parallaxScale;
                            part.parallaxMinLayers = c->material.parallaxMinLayers;
                            part.parallaxMaxLayers = c->material.parallaxMaxLayers;
                            part.cellBombScale = c->material.cellBombScale;
                            part.cellBombStrength = c->material.cellBombStrength;
                            part.cellBombRotation = c->material.cellBombRotation;
                            cached.push_back(part);
                        }
                        for (unsigned child = 0; child < node->mNumChildren; ++child)
                            walk(node->mChildren[child], world);
                    };
                walk(scene->mRootNode, aiMatrix4x4());
            }
            for (const auto &part : cached) {
                auto *e = graphics::Renderable3D::create();
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
                entities.push_back(e);
            }
        }
    } catch (const std::exception &e) { if (error) *error = e.what(); }
    return entities;
}

}  // namespace eve::housegen
