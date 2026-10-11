#include "procgen/house/HouseGen.h"

#include "common/SquirrelBinding.h"
#include "graphics/Graphics.h"
#include "model3d/Model3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <cstdlib>
#include <utility>

namespace eve::housegen {

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

void exposeHouseGeneration(ssq::Table &table) {
    const HSQUIRRELVM vm  = table.getHandle();
    auto cls = table.addClass<HouseGen>("HouseGen", std::function<HouseGen *()>([] { return new HouseGen(); }), true);
    HouseGen::expose(cls);
    auto request = table.addClass<HouseRequest>("HouseRequest", std::function<HouseRequest *()>([] { return new HouseRequest(); }), true);
    request.addFunc("setSeed", [](HouseRequest *r, int v) { r->seed = uint32_t(v); });
    request.addFunc("setPlot", [](HouseRequest *r, int w, int d) { r->width = w; r->depth = d; });
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
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "layout and output point set are required",
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
    cls.addFunc("instantiate", [vm](HouseGen *value, HouseLayout *layout, graphics::Graphics *gfx,
                                    model3d::Model3D *models) {
        if (!value || !layout || !gfx || !models)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "HouseGen, layout, graphics and model3d are required",
                                                                      "instantiate", {}, "housegen.squirrel")));
        return eve::script::projectResult(vm, value->instantiate(*layout, *gfx, *models));
    });
}

}  // namespace eve::housegen
