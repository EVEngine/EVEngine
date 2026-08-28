#include "housegen/HouseGen.h"
#include "housegen/HousePersistence.h"

#include "procgen/GeneratedArtifact.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <cstdlib>
#include <utility>

namespace eve::housegen {

Module_IMPL(HouseGen, new HouseGen());

HouseGen::HouseGen() : layouts_(std::make_unique<procgen::ArtifactStore>()) {}
HouseGen::~HouseGen() = default;

eve::Result<void> HouseGen::loadComponentsFromJson(const std::string &json) { return library_.loadFromJson(json); }
eve::Result<void> HouseGen::loadComponentsFromFile(const std::string &filename) {
    return library_.loadFromFile(filename);
}
void              HouseGen::clearComponents() { library_.clear(); }
int HouseGen::getComponentCount() const { return library_.count(); }
HouseRequest      HouseGen::newRequest() const { return {}; }
HouseLayout       HouseGen::newLayout() const { return {}; }
eve::Result<void> HouseGen::generate(const HouseRequest &request, HouseLayout &layout) {
    HouseGenerator generator(library_);
    return generator.generate(request, layout);
}

std::string HouseGen::requestBuildKey(const HouseRequest &request) const { return houseRequestBuildKeyText(request); }

std::vector<std::string> HouseGen::getStylePacks() const { return library_.completePacks(); }

std::string HouseGen::layoutBuildKey(const HouseLayout &layout) const { return houseLayoutBuildKeyText(layout); }

eve::Result<eve::ArtifactId> HouseGen::publishLayout(HouseLayout &layout) { return publishHouseLayout(*layouts_, layout); }

eve::Result<HouseLayout> HouseGen::findLayout(eve::ArtifactId id) const {
    const auto *artifact = layouts_->find(id);
    if (!artifact)
        return eve::Result<HouseLayout>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "house layout artifact is not published", "publishLayout", {},
            "housegen.persistence"));
    return restoreHouseLayout(*artifact);
}

eve::Result<eve::Value> HouseGen::snapshotLayouts() const { return layouts_->snapshotState(); }

eve::Result<void> HouseGen::restoreLayouts(const eve::Value &state) { return layouts_->restoreState(state); }

void HouseGen::clearLayouts() { layouts_->clear(); }

int HouseGen::layoutCount() const { return int(layouts_->size()); }

void HouseGen::expose(ssq::Table &table) {
    const HSQUIRRELVM vm  = table.getHandle();
    auto cls = table.addClass(name, HouseGen::create, false); expose(cls);
    auto request = table.addClass<HouseRequest>("HouseRequest", std::function<HouseRequest *()>([] { return new HouseRequest(); }), true);
    request.addFunc("setSeed", [](HouseRequest *r, int v) { r->seed = uint32_t(v); });
    request.addFunc("setPlot", [](HouseRequest *r, int w, int d) { r->width = w; r->depth = d; });
    request.addFunc("setFloors", [](HouseRequest *r, int v) { r->floors = v; });
    request.addFunc("setStyle", [](HouseRequest *r, const std::string &v) { r->style = v; });
    request.addFunc("setFootprint", [](HouseRequest *r, const std::string &v) { r->footprint = v; });
    request.addFunc("setRoof", [](HouseRequest *r, const std::string &v) { r->roof = v; });
    request.addFunc("setEntrance", [](HouseRequest *r, const std::string &v) { r->entrance = v; });
    // Comma-separated room names, e.g. "living,kitchen,bedroom".
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
    // Semicolon-separated corner points, e.g. "0,0;4,0;4,3;7,3;7,6;0,6".
    request.addFunc("setPerimeter", [](HouseRequest *r, const std::string &csv) {
        r->perimeter.clear();
        std::string segment;
        for (size_t i = 0; i <= csv.size(); ++i) {
            if (i == csv.size() || csv[i] == ';') {
                const size_t comma = segment.find(',');
                if (comma != std::string::npos) {
                    HousePolygonPoint p;
                    p.x = std::atof(segment.substr(0, comma).c_str());
                    p.y = std::atof(segment.substr(comma + 1).c_str());
                    r->perimeter.push_back(p);
                }
                segment.clear();
            } else {
                segment.push_back(csv[i]);
            }
        }
    });
    auto layout = table.addClass<HouseLayout>("HouseLayout", std::function<HouseLayout *()>([] { return new HouseLayout(); }), true);
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
    layout.addFunc("getModuleSize", [](HouseLayout *v) { return v ? v->moduleSize : 1.f; });
    layout.addFunc("getFloorHeight", [](HouseLayout *v) { return v ? v->floorHeight : 3.f; });
    layout.addFunc("getFootprintStyle", [](HouseLayout *v) { return v ? v->footprintStyle : std::string(); });
    layout.addFunc("getRoofStyle", [](HouseLayout *v) { return v ? v->roofStyle : std::string(); });
    layout.addFunc("getEntranceSide", [](HouseLayout *v) { return v ? v->entranceSide : std::string(); });
    layout.addFunc("getDiagnosticCount", [](HouseLayout *v) { return int(v->diagnostics.size()); });
    layout.addFunc("getRoomCount", [](HouseLayout *v) { return int(v->rooms.size()); });
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
    cls.addFunc("getStylePacks", [vm](HouseGen *value) {
        const auto projectPacks = [](std::vector<std::string> &&packs) {
            eve::Value::Array array;
            array.reserve(packs.size());
            for (std::string &pack : packs) array.emplace_back(std::move(pack));
            return eve::Value(std::move(array));
        };
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<std::vector<std::string>>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                               "HouseGen receiver must not be null",
                                               "getStylePacks", {}, "housegen.squirrel")),
                projectPacks);
        return eve::script::projectResult(
            vm, eve::Result<std::vector<std::string>>::success(value->getStylePacks(),
                                                               eve::Status::success(eve::StatusCode::Applied)),
            projectPacks);
    });
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
    const auto projectString = [](std::string value) { return eve::Value(std::move(value)); };
    cls.addFunc("requestBuildKey", [vm, projectString](HouseGen *value, HouseRequest *request) {
        if (!value || !request)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                        eve::DiagnosticCode::InvalidArgument,
                                                        "request is required for requestBuildKey",
                                                        "requestBuildKey", {}, "housegen.squirrel")),
                projectString);
        return eve::script::projectResult(vm, eve::Result<std::string>::success(value->requestBuildKey(*request)),
                                          projectString);
    });
    cls.addFunc("layoutBuildKey", [vm, projectString](HouseGen *value, HouseLayout *layout) {
        if (!value || !layout)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                        eve::DiagnosticCode::InvalidArgument,
                                                        "layout is required for layoutBuildKey",
                                                        "layoutBuildKey", {}, "housegen.squirrel")),
                projectString);
        return eve::script::projectResult(vm, eve::Result<std::string>::success(value->layoutBuildKey(*layout)),
                                          projectString);
    });
    cls.addFunc("publishLayout", [vm, projectString](HouseGen *value, HouseLayout *layout) {
        if (!value || !layout)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                        eve::DiagnosticCode::InvalidArgument,
                                                        "layout is required for publishLayout",
                                                        "publishLayout", {}, "housegen.squirrel")),
                projectString);
        auto published = value->publishLayout(*layout);
        if (!published.ok())
            return eve::script::projectResult(vm, eve::Result<std::string>::failure(published.status()),
                                              projectString);
        return eve::script::projectResult(
            vm, eve::Result<std::string>::success(std::move(published).takeValue().format(),
                                                  eve::Status::success(eve::StatusCode::Applied)),
            projectString);
    });
    cls.addFunc("findLayout", [vm, projectString](HouseGen *value, const std::string &idText) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                        eve::DiagnosticCode::InvalidArgument,
                                                        "HouseGen receiver must not be null",
                                                        "findLayout", {}, "housegen.squirrel")),
                projectString);
        const auto parsed = eve::ArtifactId::parse(idText);
        if (!parsed)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                        eve::DiagnosticCode::InvalidArgument,
                                                        "artifact id is not a canonical uuid",
                                                        "findLayout", {}, "housegen.squirrel")),
                projectString);
        auto found = value->findLayout(*parsed);
        if (!found.ok())
            return eve::script::projectResult(vm, eve::Result<std::string>::failure(found.status()),
                                              projectString);
        return eve::script::projectResult(
            vm, eve::Result<std::string>::success(std::move(found).takeValue().toJson(),
                                                  eve::Status::success(eve::StatusCode::Applied)),
            projectString);
    });
    cls.addFunc("snapshotLayouts", [vm, projectString](HouseGen *value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                        eve::DiagnosticCode::InvalidArgument,
                                                        "HouseGen receiver must not be null",
                                                        "snapshotLayouts", {}, "housegen.squirrel")),
                projectString);
        auto snapshot = value->snapshotLayouts();
        if (!snapshot.ok())
            return eve::script::projectResult(vm, eve::Result<std::string>::failure(snapshot.status()),
                                              projectString);
        auto json = std::move(snapshot).takeValue().toJson();
        if (!json.ok())
            return eve::script::projectResult(vm, eve::Result<std::string>::failure(json.status()),
                                              projectString);
        return eve::script::projectResult(
            vm, eve::Result<std::string>::success(std::move(json).takeValue(),
                                                  eve::Status::success(eve::StatusCode::Applied)),
            projectString);
    });
    cls.addFunc("restoreLayouts", [vm](HouseGen *value, const std::string &json) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(
                                                      eve::Diagnostic::error(
                                                          eve::DiagnosticCode::InvalidArgument,
                                                          "HouseGen receiver must not be null",
                                                          "restoreLayouts", {}, "housegen.squirrel")));
        auto state = eve::Value::fromJson(json);
        if (!state.ok())
            return eve::script::projectResult(vm, eve::Result<void>::failure(state.status()));
        return eve::script::projectResult(vm, value->restoreLayouts(std::move(state).takeValue()));
    });
    cls.addFunc("clearLayouts", &HouseGen::clearLayouts);
    cls.addFunc("layoutCount", &HouseGen::layoutCount);
}

}  // namespace eve::housegen
