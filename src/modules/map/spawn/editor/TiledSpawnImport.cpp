// Authoring CPU phase only: no ECS, simulation clocks, RNG, file I/O or runtime publication.
#include "map/spawn/editor/TiledSpawnImport.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <map>
#include <numbers>
#include <set>
#include <string_view>
#include "SpawnOverlayProjection.inl"
#include "SpawnViewportFrame.inl"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/editing/SpawnDocument.h"
#include "map/spawn/editor/SpawnForm.h"
#include "property_access/PropertyAccess.h"
#include "spawn/SpawnMap.h"
namespace eve::spawn_editor {
namespace {
struct Failure {
    Status status;
};
[[noreturn]] void reject(std::string message, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    throw Failure{Status::failure(Diagnostic::error(code, std::move(message), "spawn.tiled-import"))};
}
const Value& required(const Value& value, const char* key) {
    const auto* found = value.find(key);
    if (!found) reject(std::string("Missing Tiled field: ") + key);
    return *found;
}
std::string text(const Value& value) {
    const auto* result = value.getIf<std::string>();
    if (!result) reject("Expected Tiled text");
    return *result;
}
double number(const Value& value) {
    double result;
    if (const auto* real = value.getIf<double>())
        result = *real;
    else if (const auto* integer = value.getIf<std::int64_t>()) {
        result = static_cast<double>(*integer);
        if (result >= 0x1p63 || result < -0x1p63 || static_cast<std::int64_t>(result) != *integer)
            reject("Tiled coordinate loses integer precision");
    } else
        reject("Expected numeric Tiled coordinate");
    if (!std::isfinite(result)) reject("Nonfinite Tiled coordinate");
    return result;
}
double number(const Value& value, const char* key, double absent) {
    const auto* field = value.find(key);
    return field ? number(*field) : absent;
}
bool flag(const Value& value, const char* key) {
    const auto* field = value.find(key);
    if (!field) return false;
    const auto* result = field->getIf<bool>();
    if (!result) reject(std::string("Expected boolean Tiled field: ") + key);
    return *result;
}
const Value::Array& array(const Value& value) {
    const auto* result = value.getIf<Value::Array>();
    if (!result) reject("Expected Tiled array");
    return *result;
}
void fields(const Value& value, std::initializer_list<std::string_view> allowed) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) reject("Expected Tiled object");
    for (const auto& [key, entry] : *object) {
        (void)entry;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            reject("Unknown field in import input: " + key, DiagnosticCode::Unsupported);
    }
}
void version(const Value& value) {
    if (const auto* name = value.getIf<std::string>()) {
        unsigned minor = 0;
        if (name->size() >= 3 && name->starts_with("1.")) {
            const auto parsed = std::from_chars(name->data() + 2, name->data() + name->size(), minor);
            if (parsed.ec == std::errc{} && parsed.ptr == name->data() + name->size() && minor <= 12) return;
        }
    } else {
        const double legacy = number(value);
        for (double known : {1., 1.1, 1.2, 1.3, 1.4, 1.5})
            if (known == legacy) return;
    }
    reject("Unsupported Tiled JSON version", DiagnosticCode::UnknownVersion);
}
bool marked(const Value& value) {
    for (const char* key : {"type", "class"})
        if (const auto* field = value.find(key))
            if (const auto* name = field->getIf<std::string>(); name && *name == "eve.spawn") return true;
    return false;
}
bool containsMarked(const Value& layer, unsigned depth) {
    if (depth > 32) reject("Tiled group nesting exceeds 32");
    if (const auto* objects = layer.find("objects"))
        if (const auto* entries = objects->getIf<Value::Array>())
            for (const auto& object : *entries)
                if (marked(object)) return true;
    if (const auto* nested = layer.find("layers"))
        if (const auto* children = nested->getIf<Value::Array>())
            for (const auto& child : *children)
                if (containsMarked(child, depth + 1)) return true;
    return false;
}
struct Translator {
    map_spawn::PixelTransform      transform;
    std::string                    prefix;
    std::map<std::uint32_t, Value> instances;
    std::size_t                    layers   = 0;
    std::size_t                    vertices = 0;
    std::set<std::uint32_t>        sourceIds;
    std::set<std::string>          targets;
    Value                          defaults;
    const Value::Array&            existing;  // Synchronous borrow from prepare's owning base snapshot.
    std::size_t                    scannedLayers = 0;
    Translator(map_spawn::PixelTransform input, std::string identity, Value prototype, const Value::Array& originals)
        : transform(input), prefix(std::move(identity)), defaults(std::move(prototype)), existing(originals) {}
    void scanIds(const Value::Array& input, unsigned depth) {
        if (depth > 32) reject("Tiled group nesting exceeds 32");
        for (const auto& layer : input) {
            if (++scannedLayers > 4096) reject("Too many Tiled layers");
            const auto type = text(required(layer, "type"));
            if (type == "group") scanIds(array(required(layer, "layers")), depth + 1);
            if (type != "objectgroup") continue;
            for (const auto& object : array(required(layer, "objects"))) {
                const auto* id = required(object, "id").getIf<std::int64_t>();
                if (!id || *id <= 0 || *id > UINT32_MAX || sourceIds.size() >= 65536 ||
                    !sourceIds.insert(static_cast<std::uint32_t>(*id)).second)
                    reject("Tiled object IDs must be globally unique positive uint32 values (maximum 65536 objects)");
            }
        }
    }
    Value world(spawn::Point pixel) const {
        auto result = map_spawn::pixelToWorld(transform, pixel);
        if (!result) throw Failure{result.status()};
        auto point = std::move(result).takeValue();
        return Value::Array{point.x, point.y};
    }
    void object(const Value& value, spawn::Point offset) {
        const auto* oldField = value.find("type");
        const auto* newField = value.find("class");
        const auto* oldKind  = oldField ? oldField->getIf<std::string>() : nullptr;
        const auto* newKind  = newField ? newField->getIf<std::string>() : nullptr;
        if ((!oldKind || *oldKind != "eve.spawn") && (!newKind || *newKind != "eve.spawn")) return;
        if ((oldField && !oldKind) || (newField && !newKind) ||
            (oldKind && newKind && !oldKind->empty() && !newKind->empty() && *oldKind != *newKind))
            reject("Conflicting Tiled type/class");
        fields(value, {"id",      "type",     "class",   "name",     "visible",    "opacity", "x",
                       "y",       "width",    "height",  "rotation", "properties", "point",   "ellipse",
                       "polygon", "polyline", "capsule", "gid",      "text",       "template"});
        const auto* id = required(value, "id").getIf<std::int64_t>();
        if (!id || *id <= 0 || *id > UINT32_MAX) reject("Tiled spawn object needs a positive uint32 ID");
        if (instances.size() >= 1024 || instances.contains(static_cast<std::uint32_t>(*id)))
            reject("Too many or duplicate Tiled spawn object IDs");
        for (const char* field : {"gid", "text", "polyline", "template"})
            if (value.find(field)) reject("Unsupported Tiled spawn object geometry", DiagnosticCode::Unsupported);
        if (flag(value, "capsule")) reject("Capsule spawn region is unsupported", DiagnosticCode::Unsupported);
        std::map<std::string, std::string> properties;
        if (const auto* input = value.find("properties"))
            for (const auto& entry : array(*input)) {
                const auto name = text(required(entry, "name"));
                if (!name.starts_with("eve.spawn.")) continue;
                if (name != "eve.spawn.instance") reject("Unknown reserved Tiled spawn property: " + name);
                if (text(required(entry, "type")) != "string" ||
                    !properties.emplace(name, text(required(entry, "value"))).second)
                    reject("Duplicate or non-string Tiled spawn property");
            }
        Value instance;
        if (properties.contains("eve.spawn.instance")) {
            const auto& target = properties.at("eve.spawn.instance");
            auto        found  = std::find_if(existing.begin(), existing.end(),
                                              [&](const auto& row) { return row.find("id")->asString() == target; });
            if (found == existing.end())
                reject("Referenced sidecar instance not found: " + target, DiagnosticCode::NotFound);
            instance = *found;
        } else {
            const auto target = prefix + std::to_string(*id);
            auto       found  = std::find_if(existing.begin(), existing.end(),
                                             [&](const auto& row) { return row.find("id")->asString() == target; });
            if (found != existing.end())
                instance = *found;
            else {
                if (defaults.isNull()) reject("New Tiled objects require explicit editor instance defaults");
                instance = defaults;
                instance.set("id", target);
            }
        }
        if (!targets.insert(instance.find("id")->asString()).second)
            reject("Multiple Tiled objects address the same sidecar instance", DiagnosticCode::Conflict);
        const auto x = number(required(value, "x")), y = number(required(value, "y"));
        const auto width = number(value, "width", 0), height = number(value, "height", 0);
        const auto rotation = std::remainder(number(value, "rotation", 0), 360.) * (std::numbers::pi / 180.);
        if (width < 0 || height < 0) reject("Negative Tiled shape size");
        const double cosine = std::cos(rotation), sine = std::sin(rotation);
        auto         point = [&](double dx, double dy) {
            if (++vertices > 65536) reject("Too many imported geometry points");
            return world({offset.x + x + cosine * dx - sine * dy, offset.y + y + sine * dx + cosine * dy});
        };
        const bool  isPoint = flag(value, "point"), ellipse = flag(value, "ellipse");
        const auto* polygon = value.find("polygon");
        if (int(isPoint) + int(ellipse) + int(polygon != nullptr) > 1) reject("Conflicting Tiled shape markers");
        Value region;
        if (isPoint) {
            if (width != 0 || height != 0) reject("Tiled point must not have size");
            region = Value::object({{"kind", "points"}, {"space", "world2d"}, {"points", Value::Array{point(0, 0)}}});
        } else if (ellipse) {
            if (width <= 0 || width != height)
                reject("Only circular Tiled ellipses are representable", DiagnosticCode::Unsupported);
            region = Value::object({{"kind", "circle"},
                                    {"space", "world2d"},
                                    {"center", point(width / 2, height / 2)},
                                    {"radius", width * transform.scale / 2}});
        } else {
            Value::Array vertices;
            if (polygon) {
                const auto& input = array(*polygon);
                if (input.size() > 1024) reject("Too many polygon vertices");
                for (const auto& vertex : input) {
                    fields(vertex, {"x", "y"});
                    vertices.push_back(point(number(required(vertex, "x")), number(required(vertex, "y"))));
                }
            } else {
                if (width <= 0 || height <= 0) reject("Empty Tiled rectangle");
                vertices = {point(0, 0), point(width, 0), point(width, height), point(0, height)};
            }
            region = Value::object({{"kind", "polygon"}, {"space", "world2d"}, {"vertices", std::move(vertices)}});
        }
        instance.set("region", std::move(region));
        instances.emplace(static_cast<std::uint32_t>(*id), std::move(instance));
    }
    void walk(const Value::Array& input, spawn::Point offset, unsigned depth) {
        if (depth > 32) reject("Tiled group nesting exceeds 32");
        for (const auto& layer : input) {
            if (++layers > 4096) reject("Too many Tiled layers");
            const auto type = text(required(layer, "type"));
            if (type != "group" && type != "objectgroup") continue;
            if (!containsMarked(layer, depth)) continue;
            fields(layer, {"id",        "type",      "class",     "name",      "visible",    "opacity", "locked",
                           "tintcolor", "mode",      "x",         "y",         "width",      "height",  "offsetx",
                           "offsety",   "parallaxx", "parallaxy", "draworder", "properties", "layers",  "objects"});
            if (number(layer, "x", 0) != 0 || number(layer, "y", 0) != 0 || number(layer, "parallaxx", 1) != 1 ||
                number(layer, "parallaxy", 1) != 1)
                reject("Unsupported Tiled layer tile offset/parallax", DiagnosticCode::Unsupported);
            const spawn::Point next{offset.x + number(layer, "offsetx", 0), offset.y + number(layer, "offsety", 0)};
            if (type == "group")
                walk(array(required(layer, "layers")), next, depth + 1);
            else
                for (const auto& item : array(required(layer, "objects"))) {
                    try {
                        object(item, next);
                    } catch (const Failure& failure) {
                        std::vector<Diagnostic> diagnostics;
                        for (const auto& original : failure.status.diagnostics()) {
                            auto details = original.details();
                            details.emplace_back("field", original.path());
                            diagnostics.emplace_back(original.code(), original.severity(), original.message(),
                                                     prefix + std::to_string(required(item, "id").asInt()),
                                                     std::move(details), "spawn.tiled-import");
                        }
                        throw Failure{Status(failure.status.code(), std::move(diagnostics))};
                    }
                }
        }
    }
};
}  // namespace
struct PreparedTiledSpawnImport::Impl {
    std::weak_ptr<SpawnForm>               form;
    Revision                               revision;
    Value                                  before, candidate;
    std::vector<std::string>               ids;
    map_spawn::PixelTransform              transform;
    std::vector<spawn::InstanceDefinition> previewInstances;
    bool                                   consumed = false, busy = false;
};
PreparedTiledSpawnImport::PreparedTiledSpawnImport(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedTiledSpawnImport::~PreparedTiledSpawnImport()                                                     = default;
PreparedTiledSpawnImport::PreparedTiledSpawnImport(PreparedTiledSpawnImport&&) noexcept                   = default;
PreparedTiledSpawnImport&        PreparedTiledSpawnImport::operator=(PreparedTiledSpawnImport&&) noexcept = default;
Result<PreparedTiledSpawnImport> PreparedTiledSpawnImport::prepare(
    std::shared_ptr<SpawnForm> form, Value tiled, AssetGuid sourceAsset, const map_spawn::PixelTransform& transform,
    Value instanceDefaults, std::shared_ptr<const spawn::ITemplateResolver> templates,
    std::shared_ptr<const spawn::IUnitDefinitionResolver> units) {
    using R = Result<PreparedTiledSpawnImport>;
    try {
        if (!form || !templates || !units || sourceAsset.isNil())
            reject("Actual form/resolvers and source asset UUID required");
        auto base = form->captureSource();
        if (!base) return R::failure(base.status());
        auto map = form->captureMap();
        if (!map) return R::failure(map.status());
        if (map.value().revision != base.value().revision)
            reject("Form changed during source capture", DiagnosticCode::Conflict);
        if (text(required(tiled, "type")) != "map" || text(required(tiled, "orientation")) != "orthogonal" ||
            flag(tiled, "infinite"))
            reject("Only finite orthogonal Tiled maps are supported", DiagnosticCode::Unsupported);
        version(required(tiled, "version"));
        if (!instanceDefaults.isNull())
            fields(instanceDefaults, {"template", "activation", "inactive_policy", "unload_policy", "overrides"});
        Translator translate{transform, "tiled." + sourceAsset.format() + ".", std::move(instanceDefaults),
                             *base.value().source.find("instances")->getIf<Value::Array>()};
        auto       admittedTransform = map_spawn::pixelToWorld(transform, {0, 0});
        if (!admittedTransform) return R::failure(admittedTransform.status());
        const auto& sourceLayers = array(required(tiled, "layers"));
        translate.scanIds(sourceLayers, 0);
        translate.walk(sourceLayers, {}, 0);
        if (translate.instances.empty()) reject("No eve.spawn objects selected");
        auto state       = std::make_shared<Impl>();
        state->form      = form;
        state->revision  = base.value().revision;
        state->before    = std::move(base).takeValue().source;
        state->candidate = state->before;
        auto* entries    = state->candidate.find("instances")->getIf<Value::Array>();
        for (auto& [id, instance] : translate.instances) {
            (void)id;
            const auto key = instance.find("id")->asString();
            state->ids.push_back(key);
            auto found = std::find_if(entries->begin(), entries->end(),
                                      [&](const auto& row) { return row.find("id")->asString() == key; });
            if (found == entries->end())
                entries->push_back(std::move(instance));
            else
                *found = std::move(instance);
        }
        auto validated = spawn_editing::SpawnDocumentTarget::create(
            editing::TargetId("tiled-import-candidate"), state->candidate, std::move(templates), std::move(units));
        if (!validated) return R::failure(validated.status());
        state->candidate = validated.value()->source();
        state->transform = transform;
        for (const auto& instance : validated.value()->compiled().instances())
            if (std::find(state->ids.begin(), state->ids.end(), instance.id) != state->ids.end())
                state->previewInstances.push_back(instance);
        if (form->sourceRevision() != state->revision)
            reject("Source changed during import preparation", DiagnosticCode::Conflict);
        return R::success(PreparedTiledSpawnImport(std::move(state)));
    } catch (const Failure& failure) {
        return R::failure(failure.status);
    }
}
const Value& PreparedTiledSpawnImport::source() const noexcept {
    static const Value empty;
    return impl_ ? impl_->candidate : empty;
}
std::span<const std::string> PreparedTiledSpawnImport::importedIds() const noexcept {
    return impl_ ? std::span<const std::string>(impl_->ids) : std::span<const std::string>{};
}
Revision PreparedTiledSpawnImport::sourceRevision() const noexcept {
    return impl_ ? impl_->revision : Revision::zero();
}
Result<void> PreparedTiledSpawnImport::drawPreview(const MapViewportFrame& frame,
                                                   editor::IEditorOverlay& overlay) const {
    auto state = impl_;
    if (!state || state->busy || state->consumed)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Import plan consumed, moved or busy"));
    auto valid = detail::validateViewportFrame(frame);
    if (!valid) return valid;
    struct Guard {
        bool& busy;
        explicit Guard(bool& value) : busy(value) { busy = true; }
        ~Guard() { busy = false; }
    } guard(state->busy);
    detail::ProjectedOverlay projected(state->transform, frame, overlay);
    editor::OverlayStyle     style;
    style.color = 0xffffb040U;
    return detail::drawRegions(state->previewInstances, projected, overlay, style);
}
Result<void> PreparedTiledSpawnImport::commit() {
    auto state = impl_;
    try {
        if (!state || state->busy || state->consumed)
            reject("Import plan consumed, moved or busy", DiagnosticCode::PreconditionViolation);
        auto form = state->form.lock();
        if (!form) reject("Import form retired", DiagnosticCode::StaleHandle);
        auto current = form->captureSource();
        if (!current) return Result<void>::failure(current.status());
        if (current.value().revision != state->revision || current.value().source != state->before)
            reject("Import source revision changed", DiagnosticCode::Conflict);
        struct Guard {
            bool& busy;
            Guard(bool& value) : busy(value) { busy = true; }
            ~Guard() { busy = false; }
        } guard(state->busy);
        auto applied = form->view().write("$source", state->candidate);
        if (!applied) return applied;
        state->consumed = true;
        return applied;
    } catch (const Failure& failure) {
        return Result<void>::failure(failure.status);
    }
}
}  // namespace eve::spawn_editor
