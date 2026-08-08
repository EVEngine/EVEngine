#include "procgen/Procgen.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/JsonExport.h"
#include "procgen/Semantic.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::procgen {

Module_IMPL(Procgen, new Procgen());

Procgen::Procgen() {
    GeneratorRegistry::instance().registerBuiltins();
    // Sensible pixel-RPG default palette (games override GIDs to match tileset).
    setPaletteGid("default", "empty", 0);
    setPaletteGid("default", "wall", 1);
    setPaletteGid("default", "floor", 2);
    setPaletteGid("default", "corridor", 2);
    setPaletteGid("default", "door", 3);
    setPaletteGid("default", "water", 4);
    setPaletteGid("default", "sand", 5);
    setPaletteGid("default", "grass", 6);
    setPaletteGid("default", "dirt", 7);
    setPaletteGid("default", "stone", 8);
    setPaletteGid("default", "snow", 9);
    setPaletteGid("dungeon_default", "empty", 0);
    setPaletteGid("dungeon_default", "wall", 1);
    setPaletteGid("dungeon_default", "floor", 2);
    setPaletteGid("dungeon_default", "corridor", 2);
    setPaletteGid("dungeon_default", "door", 3);
}

Params *Procgen::newParams() { return new Params(); }
OutputSpec *Procgen::newOutput() { return new OutputSpec(); }

Grid2D *Procgen::newGrid(int width, int height) {
    auto *g = new Grid2D();
    g->resize(width, height);
    return g;
}

bool Procgen::runGenerate(const std::string &algorithmId, const Params &params, Grid2D &out) {
    lastError_.clear();
    GeneratorRegistry::instance().registerBuiltins();
    if (!GeneratorRegistry::instance().generate(algorithmId, params, out, lastError_)) {
        if (lastError_.empty()) lastError_ = "generate failed";
        return false;
    }
    return true;
}

Grid2D *Procgen::generate(const std::string &algorithmId, Params *params) {
    if (!params) {
        lastError_ = "generate: null params";
        return nullptr;
    }
    auto *grid = new Grid2D();
    if (!runGenerate(algorithmId, *params, *grid)) {
        delete grid;
        return nullptr;
    }
    return grid;
}

bool Procgen::generateTo(const std::string &algorithmId, Params *params, OutputSpec *output) {
    if (!params) {
        lastError_ = "generateTo: null params";
        return false;
    }
    if (!output) {
        lastError_ = "generateTo: null output";
        return false;
    }
    Grid2D grid;
    if (!runGenerate(algorithmId, *params, grid)) return false;

    const std::string target = output->getTarget();
    if (target == "grid") {
        lastError_ = "generateTo: target 'grid' has no sink; use generate()";
        return false;
    }
    if (target == "tilelayer") {
        return applyToLayer(&grid, output->getPalette(), output->getLayer());
    }
    if (target == "json") {
        if (!writeGridJson(grid, output->getPath(), &lastError_)) return false;
        return true;
    }
    lastError_ = "generateTo: unknown target '" + target + "' (use grid|tilelayer|json)";
    return false;
}

bool Procgen::applyToLayer(Grid2D *grid, const std::string &palette, map::TileLayer *layer) {
    if (!grid) {
        lastError_ = "applyToLayer: null grid";
        return false;
    }
    if (!palettes_.applyToLayer(*grid, palette, layer, &lastError_)) return false;
    return true;
}

void Procgen::setPaletteGid(const std::string &palette, const std::string &semantic, int gid) {
    palettes_.setGid(palette, semantic, gid);
}

int Procgen::getPaletteGid(const std::string &palette, const std::string &semantic) const {
    return palettes_.getGid(palette, semantic);
}

int Procgen::getAlgorithmCount() const {
    algorithmIdsCache_ = GeneratorRegistry::instance().list();
    return int(algorithmIdsCache_.size());
}

std::string Procgen::getAlgorithmId(int index) const {
    if (algorithmIdsCache_.empty()) algorithmIdsCache_ = GeneratorRegistry::instance().list();
    if (index < 0 || index >= int(algorithmIdsCache_.size())) return {};
    return algorithmIdsCache_[size_t(index)];
}

bool Procgen::hasAlgorithm(const std::string &algorithmId) const {
    return GeneratorRegistry::instance().has(algorithmId);
}

std::string Procgen::lastError() const { return lastError_; }

std::string Procgen::gridToJson(Grid2D *grid) const {
    if (!grid) return "{}";
    return eve::procgen::gridToJson(*grid);
}

void Procgen::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Procgen::create, false);
    expose(cls);

    auto params = table.addClass<Params>(
        "ProcgenParams", std::function<Params *()>([]() -> Params * { return nullptr; }), true);
    params.addFunc("setSeed", &Params::setSeed);
    params.addFunc("getSeed", &Params::getSeed);
    params.addFunc("setSize", &Params::setSize);
    params.addFunc("getWidth", &Params::getWidth);
    params.addFunc("getHeight", &Params::getHeight);
    params.addFunc("setInt", &Params::setInt);
    params.addFunc("setFloat", &Params::setFloat);
    params.addFunc("setString", &Params::setString);
    params.addFunc("has", &Params::has);
    params.addFunc("getInt", &Params::getInt);
    params.addFunc("getFloat", &Params::getFloat);
    params.addFunc("getString", &Params::getString);

    auto output = table.addClass<OutputSpec>(
        "ProcgenOutput", std::function<OutputSpec *()>([]() -> OutputSpec * { return nullptr; }),
        true);
    output.addFunc("setTarget", &OutputSpec::setTarget);
    output.addFunc("getTarget", &OutputSpec::getTarget);
    output.addFunc("setLayer", &OutputSpec::setLayer);
    output.addFunc("getLayer", &OutputSpec::getLayer);
    output.addFunc("setPalette", &OutputSpec::setPalette);
    output.addFunc("getPalette", &OutputSpec::getPalette);
    output.addFunc("setPath", &OutputSpec::setPath);
    output.addFunc("getPath", &OutputSpec::getPath);

    auto grid = table.addClass<Grid2D>(
        "ProcgenGrid2D", std::function<Grid2D *()>([]() -> Grid2D * { return nullptr; }), true);
    grid.addFunc("resize", &Grid2D::resize);
    grid.addFunc("getWidth", &Grid2D::getWidth);
    grid.addFunc("getHeight", &Grid2D::getHeight);
    grid.addFunc("setCell", &Grid2D::setCell);
    grid.addFunc("getCell", &Grid2D::getCell);
    grid.addFunc("fill", &Grid2D::fill);
    grid.addFunc("setMeta", &Grid2D::setMeta);
    grid.addFunc("getMeta", &Grid2D::getMeta);
    grid.addFunc("clearObjects", &Grid2D::clearObjects);
    grid.addFunc("addObjectAt", &Grid2D::addObjectAt);
    grid.addFunc("addObject", &Grid2D::addObject);
    grid.addFunc("getObjectCount", &Grid2D::getObjectCount);
    grid.addFunc("getObjectName", &Grid2D::getObjectName);
    grid.addFunc("getObjectType", &Grid2D::getObjectType);
    grid.addFunc("getObjectX", &Grid2D::getObjectX);
    grid.addFunc("getObjectY", &Grid2D::getObjectY);
    grid.addFunc("getObjectWidth", &Grid2D::getObjectWidth);
    grid.addFunc("getObjectHeight", &Grid2D::getObjectHeight);
    grid.addFunc("getObjectGid", &Grid2D::getObjectGid);
}

void Procgen::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Procgen::getName);
    cls.addFunc("newParams", &Procgen::newParams);
    cls.addFunc("newOutput", &Procgen::newOutput);
    cls.addFunc("newGrid", &Procgen::newGrid);
    cls.addFunc("generate", &Procgen::generate);
    cls.addFunc("generateTo", &Procgen::generateTo);
    cls.addFunc("applyToLayer", &Procgen::applyToLayer);
    cls.addFunc("setPaletteGid", &Procgen::setPaletteGid);
    cls.addFunc("getPaletteGid", &Procgen::getPaletteGid);
    cls.addFunc("getAlgorithmCount", &Procgen::getAlgorithmCount);
    cls.addFunc("getAlgorithmId", &Procgen::getAlgorithmId);
    cls.addFunc("hasAlgorithm", &Procgen::hasAlgorithm);
    cls.addFunc("lastError", &Procgen::lastError);
    cls.addFunc("gridToJson", &Procgen::gridToJson);
}

}  // namespace eve::procgen
