#include "housegen/HouseGen.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <utility>

namespace eve::housegen {

Module_IMPL(HouseGen, new HouseGen());

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
    layout.addFunc("getDiagnosticCount", [](HouseLayout *v) { return int(v->diagnostics.size()); });
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
}

}  // namespace eve::housegen
