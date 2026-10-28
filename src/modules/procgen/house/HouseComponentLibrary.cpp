#include "procgen/house/HouseComponentLibrary.h"
#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/scene.h>
#include <assimp/texture.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/mat4x4.hpp>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "common/Json.h"
#include "common/SquirrelBinding.h"
#include "data/ByteData.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "procgen/Semantic.h"
#include "procgen/house/HouseGen.h"
#include "procgen/house/HouseLayout.h"

// HouseComponentLibrary.cpp
namespace eve::housegen {
namespace {

using eve::json::Value;

/** Assimp row-major → glm column-major for IResourceFactory::newMeshFromAssimp. */
glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

eve::Result<SocketDirection> parseDirection(std::string_view value) {
    if (value == "north") return eve::Result<SocketDirection>::success(SocketDirection::North);
    if (value == "east") return eve::Result<SocketDirection>::success(SocketDirection::East);
    if (value == "south") return eve::Result<SocketDirection>::success(SocketDirection::South);
    if (value == "west") return eve::Result<SocketDirection>::success(SocketDirection::West);
    if (value == "up") return eve::Result<SocketDirection>::success(SocketDirection::Up);
    if (value == "down") return eve::Result<SocketDirection>::success(SocketDirection::Down);
    return eve::Result<SocketDirection>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "socket direction is invalid", "direction", {}, "housegen.components"));
}

eve::Result<HouseComponent> parseComponent(Value object) {
    if (!object.isObject())
        return eve::Result<HouseComponent>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "component must be an object", {}, {}, "housegen.components"));

    HouseComponent out;
    out.id        = object.getString("id");
    out.modelPath = object.getString("model");
    out.category  = object.getString("category");
    out.width     = object.getInt("width", 1);
    out.depth     = object.getInt("depth", 1);
    out.height    = object.getInt("height", 1);
    out.weight    = object.getInt("weight", 1);
    out.tags      = object.getStringArray("tags");
    if (object.get("rotations").isArray()) out.rotations = object.getIntArray("rotations");

    const Value sockets = object.get("sockets");
    for (size_t i = 0; i < sockets.size(); ++i) {
        const Value socketObject = sockets.at(i);
        if (!socketObject.isObject())
            return eve::Result<HouseComponent>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "socket must be an object", "sockets", {}, "housegen.components"));
        auto direction = parseDirection(socketObject.getString("direction"));
        if (!direction.ok()) return eve::Result<HouseComponent>::failure(direction.status());

        HouseSocket socket;
        socket.direction = std::move(direction).takeValue();
        socket.type      = socketObject.getString("type");
        socket.accepts   = socketObject.getStringArray("accepts");
        if (socket.type.empty())
            return eve::Result<HouseComponent>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "socket needs a valid direction and type",
                                       "sockets", {}, "housegen.components"));
        out.sockets.push_back(std::move(socket));
    }

    const Value material = object.get("material");
    if (material.isObject()) {
        const Value color = material.get("baseColor");
        if (color.isArray()) {
            if (color.size() != 3 && color.size() != 4)
                return eve::Result<HouseComponent>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "material baseColor needs 3 or 4 values",
                    "material.baseColor", {}, "housegen.components"));
            out.material.hasBaseColor = true;
            out.material.baseColorR   = color.at(0).asFloat();
            out.material.baseColorG   = color.at(1).asFloat();
            out.material.baseColorB   = color.at(2).asFloat();
            out.material.baseColorA   = color.size() == 4 ? color.at(3).asFloat() : 1.f;
        }
        out.material.baseColorTexture = material.getString("baseColorTexture");
        out.material.normalTexture    = material.getString("normalTexture");
        out.material.heightTexture    = material.getString("heightTexture");
        if (material.has("metallic")) {
            out.material.hasMetallic = true;
            out.material.metallic    = material.getFloat("metallic");
        }
        if (material.has("roughness")) {
            out.material.hasRoughness = true;
            out.material.roughness    = material.getFloat("roughness");
        }
        out.material.parallaxScale     = material.getFloat("parallaxScale", 0.f);
        out.material.parallaxMinLayers = material.getFloat("parallaxMinLayers", 8.f);
        out.material.parallaxMaxLayers = material.getFloat("parallaxMaxLayers", 32.f);
        out.material.cellBombScale     = material.getFloat("cellBombScale", 4.f);
        out.material.cellBombStrength  = material.getFloat("cellBombStrength", 0.f);
        out.material.cellBombRotation  = material.getFloat("cellBombRotation", 1.f);
    }
    return eve::Result<HouseComponent>::success(std::move(out));
}

}  // namespace

eve::Result<void> HouseComponentLibrary::registerComponent(const HouseComponent &component) {
    if (component.id.empty() || component.modelPath.empty() || component.category.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "component needs id, model and category", {}, {},
                                                                 "housegen.components"));
    if (component.width < 1 || component.depth < 1 || component.height < 1 || component.weight < 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "component dimensions and weight must be positive", {},
                                                                 {}, "housegen.components"));
    for (const int rotation : component.rotations) {
        if (rotation < 0 || rotation >= 360 || rotation % 90 != 0)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "rotations must be cardinal degrees", "rotations",
                                                                     {}, "housegen.components"));
    }

    const auto inUnitRange = [](float value) { return value >= 0.f && value <= 1.f; };
    if ((component.material.hasBaseColor &&
         (!inUnitRange(component.material.baseColorR) || !inUnitRange(component.material.baseColorG) ||
          !inUnitRange(component.material.baseColorB) || !inUnitRange(component.material.baseColorA))) ||
        (component.material.hasMetallic && !inUnitRange(component.material.metallic)) ||
        (component.material.hasRoughness && !inUnitRange(component.material.roughness)) ||
        !inUnitRange(component.material.cellBombStrength) || !inUnitRange(component.material.cellBombRotation))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "material values must be between 0 and 1", "material",
                                                                 {}, "housegen.components"));
    if (component.material.parallaxScale < 0.f || component.material.parallaxScale > 0.2f ||
        component.material.parallaxMinLayers < 1.f ||
        component.material.parallaxMaxLayers < component.material.parallaxMinLayers ||
        component.material.cellBombScale <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "material sampling parameters are invalid", "material",
                                                                 {}, "housegen.components"));
    if (components_.contains(component.id))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "duplicate component id: " + component.id, "id", {}, "housegen.components"));

    components_.emplace(component.id, component);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> HouseComponentLibrary::loadFromJson(std::string_view json) {
    std::string               parseError;
    const eve::json::Document document = eve::json::Document::parse(std::string(json), &parseError);
    if (!document.valid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, parseError.empty() ? "invalid component manifest" : parseError, {}, {},
            "housegen.components"));

    const Value root   = document.root();
    const Value values = root.isArray() ? root : root.get("components");
    if (!values.isArray())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "expected components array", "components", {}, "housegen.components"));

    HouseComponentLibrary candidate;
    for (size_t i = 0; i < values.size(); ++i) {
        auto component = parseComponent(values.at(i));
        if (!component.ok()) return eve::Result<void>::failure(component.status());
        auto registered = candidate.registerComponent(std::move(component).takeValue());
        if (!registered.ok()) return registered;
    }
    components_ = std::move(candidate.components_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> HouseComponentLibrary::loadFromFile(std::string_view filename) {
    const std::string path(filename);
    std::ifstream     input(path, std::ios::binary);
    if (!input)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "cannot open component manifest: " + path, path, {}, "housegen.components"));

    std::ostringstream json;
    json << input.rdbuf();
    HouseComponentLibrary candidate;
    auto                  loaded = candidate.loadFromJson(json.str());
    if (!loaded.ok()) return loaded;

    const std::filesystem::path assetRoot = std::filesystem::absolute(std::filesystem::path(path)).parent_path();
    for (auto &[_, component] : candidate.components_) {
        std::filesystem::path model(component.modelPath);
        if (model.is_relative()) component.modelPath = (assetRoot / model).lexically_normal().string();
    }
    components_ = std::move(candidate.components_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void HouseComponentLibrary::clear() { components_.clear(); }

eve::OptionalRef<const HouseComponent> HouseComponentLibrary::find(std::string_view id) const {
    const auto it = components_.find(std::string(id));
    if (it == components_.end()) return {};
    return std::cref(it->second);
}

int HouseComponentLibrary::count() const { return int(components_.size()); }

std::vector<std::string> HouseComponentLibrary::ids() const {
    std::vector<std::string> result;
    result.reserve(components_.size());
    for (const auto &[id, _] : components_) result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::reference_wrapper<const HouseComponent>> HouseComponentLibrary::byCategory(
    std::string_view category, std::string_view style) const {
    std::vector<std::reference_wrapper<const HouseComponent>> result;
    for (const auto &[_, component] : components_) {
        if (component.category == category &&
            (style.empty() || std::find(component.tags.begin(), component.tags.end(), style) != component.tags.end()))
            result.push_back(std::cref(component));
    }
    if (result.empty() && !style.empty()) return byCategory(category, {});
    std::sort(result.begin(), result.end(),
              [](const auto &left, const auto &right) { return left.get().id < right.get().id; });
    return result;
}

bool HouseComponentLibrary::hasCompletePack(std::string_view style) const {
    if (style.empty()) return false;
    static constexpr const char *core[] = {"foundation", "floor", "wall", "door", "roof"};
    for (const char *category : core) {
        bool present = false;
        for (const auto &[_, component] : components_)
            if (component.category == category &&
                std::find(component.tags.begin(), component.tags.end(), style) != component.tags.end()) {
                present = true;
                break;
            }
        if (!present) return false;
    }
    return true;
}

std::vector<std::string> HouseComponentLibrary::completePacks() const {
    std::vector<std::string> styles;
    for (const auto &[_, component] : components_)
        for (const std::string &tag : component.tags)
            if (std::find(styles.begin(), styles.end(), tag) == styles.end()) styles.push_back(tag);
    std::vector<std::string> result;
    for (const std::string &style : styles)
        if (hasCompletePack(style)) result.push_back(style);
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace eve::housegen

// HouseGen.cpp
namespace eve::housegen {

eve::Result<void> HouseGen::loadComponentsFromJson(const std::string &json) { return library_.loadFromJson(json); }
eve::Result<void> HouseGen::loadComponentsFromFile(const std::string &filename) {
    return library_.loadFromFile(filename);
}
void              HouseGen::clearComponents() { library_.clear(); }
int               HouseGen::getComponentCount() const { return library_.count(); }
HouseRequest      HouseGen::newRequest() const { return {}; }
HouseLayout       HouseGen::newLayout() const { return {}; }
eve::Result<void> HouseGen::generate(const HouseRequest &request, HouseLayout &layout) {
    HouseGenerator generator(library_);
    return generator.generate(request, layout);
}

void exposeHouseGeneration(ssq::Table &table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto cls = table.addClass<HouseGen>("HouseGen", std::function<HouseGen *()>([] { return new HouseGen(); }), true);
    HouseGen::expose(cls);
    auto request = table.addClass<HouseRequest>(
        "HouseRequest", std::function<HouseRequest *()>([] { return new HouseRequest(); }), true);
    request.addFunc("setSeed", [](HouseRequest *r, int v) { r->seed = uint32_t(v); });
    request.addFunc("setPlot", [](HouseRequest *r, int w, int d) {
        r->width = w;
        r->depth = d;
    });
    request.addFunc("setFloors", [](HouseRequest *r, int v) { r->floors = v; });
    request.addFunc("setModuleSize", [](HouseRequest *r, float v) { r->moduleSize = v; });
    request.addFunc("setFloorHeight", [](HouseRequest *r, float v) { r->floorHeight = v; });
    request.addFunc("setStyle", [](HouseRequest *r, const std::string &v) { r->style = v; });
    request.addFunc("setFootprint", [](HouseRequest *r, const std::string &v) { r->footprint = v; });
    request.addFunc("setRoof", [](HouseRequest *r, const std::string &v) { r->roof = v; });
    request.addFunc("setEntrance", [](HouseRequest *r, const std::string &v) { r->entrance = v; });
    request.addFunc("setRequiredRooms", [](HouseRequest *r, const std::string &csv) {
        r->requiredRooms.clear();
        std::string current;
        for (const char c : csv) {
            if (c == ',') {
                if (!current.empty()) r->requiredRooms.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) r->requiredRooms.push_back(current);
    });
    request.addFunc("setPerimeter", [](HouseRequest *r, const std::string &csv) {
        r->perimeter.clear();
        std::string segment;
        for (size_t i = 0; i <= csv.size(); ++i) {
            if (i == csv.size() || csv[i] == ';') {
                const size_t comma = segment.find(',');
                if (comma != std::string::npos)
                    r->perimeter.push_back({float(std::atof(segment.substr(0, comma).c_str())),
                                            float(std::atof(segment.substr(comma + 1).c_str()))});
                segment.clear();
            } else {
                segment.push_back(csv[i]);
            }
        }
    });
    auto layout = table.addClass<HouseLayout>("HouseLayout",
                                              std::function<HouseLayout *()>([] { return new HouseLayout(); }), true);
    layout.addFunc("toJson", &HouseLayout::toJson);
    layout.addFunc("fromJson", [vm](HouseLayout *value, const std::string &json) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "layout receiver must not be null", "layout", {},
                                                                      "housegen.squirrel")));
        return eve::script::projectResult(vm, value->fromJson(json));
    });
    layout.addFunc("getInstanceCount", [](HouseLayout *v) { return int(v->instances.size()); });
    layout.addFunc("getInstanceComponentId", [](HouseLayout *v, int i) {
        return (v && i >= 0 && i < int(v->instances.size())) ? v->instances[i].componentId : std::string();
    });
    layout.addFunc("getInstanceX", [](HouseLayout *v, int i) {
        return (v && i >= 0 && i < int(v->instances.size())) ? v->instances[i].x : 0;
    });
    layout.addFunc("getInstanceY", [](HouseLayout *v, int i) {
        return (v && i >= 0 && i < int(v->instances.size())) ? v->instances[i].y : 0;
    });
    layout.addFunc("getInstanceZ", [](HouseLayout *v, int i) {
        return (v && i >= 0 && i < int(v->instances.size())) ? v->instances[i].z : 0;
    });
    layout.addFunc("getInstanceRotationDeg", [](HouseLayout *v, int i) {
        return (v && i >= 0 && i < int(v->instances.size())) ? v->instances[i].rotationDeg : 0;
    });
    layout.addFunc("getFloorHeight", [](HouseLayout *v) { return v ? v->floorHeight : 3.f; });
    layout.addFunc("getFootprintStyle", [](HouseLayout *v) { return v ? v->footprintStyle : std::string(); });
    layout.addFunc("getRoofStyle", [](HouseLayout *v) { return v ? v->roofStyle : std::string(); });
    layout.addFunc("getRoomCount", [](HouseLayout *v) { return v ? int(v->rooms.size()) : 0; });
    layout.addFunc("getDiagnosticCount", [](HouseLayout *v) { return int(v->diagnostics.size()); });
    layout.addFunc("writeFootprintGrid", [vm](HouseLayout *value, procgen::Grid2D *output) {
        if (!value || !output)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "layout and output grid are required",
                                                                      "writeFootprintGrid", {}, "housegen.squirrel")));
        return eve::script::projectResult(vm, value->writeFootprintGrid(*output));
    });
    layout.addFunc("writeComponentPoints", [vm](HouseLayout *value, procgen::PointSet *output) {
        if (!value || !output)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "layout and output point set are required",
                        "writeComponentPoints", {}, "housegen.squirrel")));
        return eve::script::projectResult(vm, value->writeComponentPoints(*output));
    });
}
eve::Result<void> HouseGen::instantiate(const HouseLayout &layout, graphics::Graphics &gfx,
                                        model3d::Model3D &models) const {
    auto result = layout.instantiate(gfx, models, library_);
    return result.ok() ? eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied))
                       : eve::Result<void>::failure(result.status());
}

void HouseGen::expose(ssq::Class &cls) {
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("loadComponentsFromJson", [vm](HouseGen *value, const std::string &json) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "HouseGen receiver must not be null", "housegen",
                                                                      {}, "housegen.squirrel")));
        return eve::script::projectResult(vm, value->loadComponentsFromJson(json));
    });
    cls.addFunc("loadComponentsFromFile", [vm](HouseGen *value, const std::string &filename) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "HouseGen receiver must not be null", "housegen",
                                                                      {}, "housegen.squirrel")));
        return eve::script::projectResult(vm, value->loadComponentsFromFile(filename));
    });
    cls.addFunc("clearComponents", &HouseGen::clearComponents);
    cls.addFunc("getComponentCount", &HouseGen::getComponentCount);
    cls.addFunc("newRequest", &HouseGen::newRequest);
    cls.addFunc("newLayout", &HouseGen::newLayout);
    cls.addFunc("generate", [vm](HouseGen *value, HouseRequest *request, HouseLayout *layout) {
        if (!value || !request || !layout)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "HouseGen, request and layout are required",
                                                                      "generate", {}, "housegen.squirrel")));
        return eve::script::projectResult(vm, value->generate(*request, *layout));
    });
    cls.addFunc(
        "instantiate", [vm](HouseGen *value, HouseLayout *layout, graphics::Graphics *gfx, model3d::Model3D *models) {
            if (!value || !layout || !gfx || !models)
                return eve::script::projectResult(
                    vm, eve::Result<void>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "HouseGen, layout, graphics and model3d are required",
                            "instantiate", {}, "housegen.squirrel")));
            return eve::script::projectResult(vm, value->instantiate(*layout, *gfx, *models));
        });
}

}  // namespace eve::housegen

// HouseLayout.cpp
namespace eve::housegen {
namespace {
std::string esc(const std::string &v) {
    std::string o;
    for (char c : v) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    return o;
}

graphics::Texture *textureFromEmbedded(graphics::Graphics *gfx, const aiTexture *source) {
    if (!source || !source->pcData) return nullptr;
    if (source->mHeight == 0) {
        data::ByteData                    bytes(source->pcData, static_cast<size_t>(source->mWidth));
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
            std::ifstream         input(resolved, std::ios::binary | std::ios::ate);
            const std::streamsize size = input.tellg();
            if (size <= 0) return nullptr;
            input.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            if (!input.read(reinterpret_cast<char *>(bytes.data()), size)) return nullptr;
            data::ByteData                    source(bytes.data(), bytes.size());
            std::unique_ptr<image::ImageData> decoded(image::Image::create()->newImageData(&source));
            return gfx->newTexture(decoded.get(), graphics::TextureCreateInfo::withMipmaps(true));
        }
        return gfx->newTextureFromFile(resolved.lexically_normal().string());
    } catch (...) {
        return nullptr;
    }
}

graphics::Texture *assimpTexture(graphics::Graphics *gfx, const aiScene *scene, const aiMaterial *material,
                                 const std::string &modelPath, aiTextureType primary, aiTextureType fallback) {
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

/**
 * @brief One layout model: either a detached decode or a pinned cache entry.
 * @ownership `owned` is the only owner when the model was decoded here; `pinned`
 *            keeps a cache-owned model alive while this layout references it.
 * @lifetime The handle must not outlive the resource cache that produced `pinned`.
 */
struct ModelHandle {
    eve::script::Owned<model3d::ModelData> owned;
    eve::ResourcePin                       pinned;

    /** @brief Borrows the held model, or null when nothing was loaded. */
    [[nodiscard]] model3d::ModelData *get() const noexcept {
        // The cache registry stores Resource, so a pinned entry needs the same explicit
        // downcast the cache's own loaders use.
        return owned ? owned.get() : static_cast<model3d::ModelData *>(pinned.get());
    }
};

ModelHandle loadModel(model3d::Model3D *models, const std::string &path) {
    if (!std::filesystem::is_regular_file(std::filesystem::path(path))) {
        // The module hands back the cache-owned resource, so pin it: this layout keeps
        // using the model after the cache entry may have been unloaded.
        model3d::ModelData *cached = models->newModelDataFromFile(path);
        if (cached == nullptr) return {};
        auto pinned = eve::ResourceManager::getInstance().pin(cached);
        if (!pinned.ok()) return {};
        ModelHandle handle;
        handle.pinned = std::move(pinned).takeValue();
        return handle;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open model: " + path);
    const std::streamsize size = input.tellg();
    if (size <= 0) throw std::runtime_error("model is empty: " + path);
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(bytes.data()), size))
        throw std::runtime_error("cannot read model: " + path);
    data::ByteData source(bytes.data(), bytes.size());
    ModelHandle    handle;
    handle.owned.reset(models->newModelData(&source, std::filesystem::path(path).extension().string()));
    return handle;
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
    for (size_t i = 0; i < instances.size(); ++i) {
        const auto &v = instances[i];
        if (i) o << ',';
        o << "{\"componentId\":\"" << esc(v.componentId) << "\",\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z
          << ",\"rotationDeg\":" << v.rotationDeg << '}';
    }
    o << "],\"rooms\":[";
    for (size_t i = 0; i < rooms.size(); ++i) {
        const auto &v = rooms[i];
        if (i) o << ',';
        o << "{\"type\":\"" << esc(v.type) << "\",\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z
          << ",\"width\":" << v.width << ",\"depth\":" << v.depth << '}';
    }
    o << "],\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i) o << ',';
        o << '"' << esc(diagnostics[i]) << '"';
    }
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
    parsed.seed                     = static_cast<unsigned>(o.getInt("seed", 1));
    parsed.moduleSize               = o.getFloat("moduleSize", 1.f);
    parsed.floorHeight              = o.getFloat("floorHeight", 3.f);
    parsed.footprintStyle           = o.getString("footprintStyle", "rectangle");
    parsed.roofStyle                = o.getString("roofStyle", "gable");
    parsed.entranceSide             = o.getString("entranceSide", "north");
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
        parsed.instances.push_back(
            {v.getString("componentId"), v.getInt("x"), v.getInt("y"), v.getInt("z"), v.getInt("rotationDeg", 0)});
    }

    const Value rooms = o.get("rooms");
    for (size_t i = 0; i < rooms.size(); ++i) {
        const Value v = rooms.at(i);
        if (!v.has("type") || !v.has("x") || !v.has("y") || !v.has("width") || !v.has("depth"))
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                     "room needs type, x, y, width and depth", "rooms",
                                                                     {}, "housegen.layout"));
        parsed.rooms.push_back({v.getString("type"), v.getInt("x"), v.getInt("y"), v.getInt("z", 0), v.getInt("width"),
                                v.getInt("depth")});
    }

    parsed.diagnostics = o.getStringArray("diagnostics");
    *this              = std::move(parsed);
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
    for (const auto &instance : instances)
        if (instance.x >= footprintWidth || instance.y >= footprintDepth ||
            !footprintMask[size_t(instance.y * footprintWidth + instance.x)])
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "house instance origin is outside the canonical footprint",
                instance.componentId, {}, "housegen.layout"));
    for (const HouseRoom &room : rooms) {
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
    std::unordered_set<std::string>        occupied;
    std::unordered_set<std::string>        floorCells, roofCells;
    std::vector<std::tuple<int, int, int>> floors;
    auto                                   cellKey = [](int x, int y, int z) {
        return std::to_string(x) + ":" + std::to_string(y) + ":" + std::to_string(z);
    };
    bool entrance = false, roof = false;
    for (const auto &i : instances) {
        const auto component = library.find(i.componentId);
        if (!component)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                     "unknown component: " + i.componentId,
                                                                     "componentId", {}, "housegen.layout"));
        const HouseComponent &c                  = component->get();
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
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < w; ++x) {
                // Boundary cells legitimately carry two perpendicular wall modules at corners.
                const bool orientedBoundary = c.category == "wall" || c.category == "door" ||
                                              c.category == "interior_wall" || c.category == "interior_door";
                const std::string orientation =
                    orientedBoundary ? ":" + std::to_string((i.rotationDeg % 360 + 360) % 360) : "";
                const std::string key = std::to_string(i.x + x) + ":" + std::to_string(i.y + y) + ":" +
                                        std::to_string(i.z) + ":" + c.category + orientation;
                if (!occupied.insert(key).second)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                               "overlapping " + c.category + " components", {}, {}, "housegen.layout"));
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
        if (!floorCells.contains(cellKey(x, y, z + 1)) && !roofCells.contains(cellKey(x, y, z + 1))) {
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
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "house instance has a negative grid coordinate",
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
    for (const auto &instance : instances) {
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
    std::uint64_t       ordinal        = 0;
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
    std::unordered_map<std::string, ModelHandle> data;
    struct CachedPart {
        graphics::Mesh    *mesh          = nullptr;
        graphics::Texture *texture       = nullptr;
        graphics::Texture *normalTexture = nullptr;
        graphics::Texture *heightTexture = nullptr;
        float              r = 1.f, g = 1.f, b = 1.f, a = 1.f;
        float              metallic = 0.f, roughness = 0.72f;
        float              parallaxScale = 0.f, parallaxMinLayers = 8.f, parallaxMaxLayers = 32.f;
        float              cellBombScale = 4.f, cellBombStrength = 0.f, cellBombRotation = 1.f;
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
            // Loads once per path: the handle either owns the decode or pins the cache entry.
            auto modelIt = data.find(c.modelPath);
            if (modelIt == data.end()) modelIt = data.emplace(c.modelPath, loadModel(&models, c.modelPath)).first;
            model3d::ModelData *model = modelIt->second.get();
            // Material overrides belong to a component, so two components may safely reuse the
            // same GLB with different architectural finishes.
            auto &cached = parts[c.id];
            if (cached.empty()) {
                const aiScene *scene = model->getScene();
                if (!scene || !scene->mRootNode) throw std::runtime_error("model has no scene root: " + c.modelPath);
                std::function<void(const aiNode *, const aiMatrix4x4 &)> walk = [&](const aiNode      *node,
                                                                                    const aiMatrix4x4 &parent) {
                    const aiMatrix4x4 world = parent * node->mTransformation;
                    for (unsigned ni = 0; ni < node->mNumMeshes; ++ni) {
                        const unsigned mi = node->mMeshes[ni];
                        if (mi >= scene->mNumMeshes || !scene->mMeshes[mi]) continue;
                        const aiMesh *source = scene->mMeshes[mi];
                        CachedPart    part;
                        part.mesh = gfx.newMeshFromAssimp(*source, aiToGlm(world));
                        if (scene->mMaterials && source->mMaterialIndex < scene->mNumMaterials) {
                            const aiMaterial *material = scene->mMaterials[source->mMaterialIndex];
                            aiColor4D         color(1.f, 1.f, 1.f, 1.f);
                            material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
                            material->Get(AI_MATKEY_BASE_COLOR, color);
                            part.r = color.r;
                            part.g = color.g;
                            part.b = color.b;
                            part.a = color.a;
                            material->Get(AI_MATKEY_METALLIC_FACTOR, part.metallic);
                            material->Get(AI_MATKEY_ROUGHNESS_FACTOR, part.roughness);
                            part.texture = assimpTexture(&gfx, scene, material, c.modelPath, aiTextureType_BASE_COLOR,
                                                         aiTextureType_DIFFUSE);
                            part.normalTexture = assimpTexture(&gfx, scene, material, c.modelPath,
                                                               aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA);
                            part.heightTexture = assimpTexture(&gfx, scene, material, c.modelPath, aiTextureType_HEIGHT,
                                                               aiTextureType_DISPLACEMENT);
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
                    for (unsigned child = 0; child < node->mNumChildren; ++child) walk(node->mChildren[child], world);
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
                e->setTexCellBomb(part.cellBombScale, part.cellBombStrength, part.cellBombRotation);
                if (part.heightTexture && part.parallaxScale > 0.f)
                    e->setParallax(part.parallaxScale, part.parallaxMinLayers, part.parallaxMaxLayers);
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
