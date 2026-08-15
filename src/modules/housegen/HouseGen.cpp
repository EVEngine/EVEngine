#include "housegen/HouseGen.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::housegen {

Module_IMPL(HouseGen, new HouseGen());

bool HouseGen::loadComponentsFromJson(const std::string &json) { return library_.loadFromJson(json, &lastError_); }
bool HouseGen::loadComponentsFromFile(const std::string &filename) { return library_.loadFromFile(filename, &lastError_); }
void HouseGen::clearComponents() { library_.clear(); lastError_.clear(); }
int HouseGen::getComponentCount() const { return library_.count(); }
HouseRequest *HouseGen::newRequest() { return new HouseRequest(); }
HouseLayout *HouseGen::newLayout() { return new HouseLayout(); }
bool HouseGen::generate(HouseRequest *request, HouseLayout *layout) {
    if (!request || !layout) { lastError_ = "request and layout are required"; return false; }
    HouseGenerator generator(&library_);
    return generator.generate(*request, *layout, &lastError_);
}
std::string HouseGen::lastError() const { return lastError_; }

void HouseGen::expose(ssq::Table &table) {
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
    layout.addFunc("fromJson", [](HouseLayout *v, const std::string &json) { return v->fromJson(json, nullptr); });
    layout.addFunc("getInstanceCount", [](HouseLayout *v) { return int(v->instances.size()); });
    layout.addFunc("getDiagnosticCount", [](HouseLayout *v) { return int(v->diagnostics.size()); });
}

void HouseGen::expose(ssq::Class &cls) {
    cls.addFunc("loadComponentsFromJson", &HouseGen::loadComponentsFromJson);
    cls.addFunc("loadComponentsFromFile", &HouseGen::loadComponentsFromFile);
    cls.addFunc("clearComponents", &HouseGen::clearComponents);
    cls.addFunc("getComponentCount", &HouseGen::getComponentCount);
    cls.addFunc("newRequest", &HouseGen::newRequest);
    cls.addFunc("newLayout", &HouseGen::newLayout);
    cls.addFunc("generate", &HouseGen::generate);
    cls.addFunc("lastError", &HouseGen::lastError);
}

}  // namespace eve::housegen
