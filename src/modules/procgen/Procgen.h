#pragma once

#include "common/Module.h"
#include "procgen/Grid2D.h"
#include "procgen/OutputSpec.h"
#include "procgen/Palette.h"
#include "procgen/Params.h"

#include <string>
#include <vector>

namespace eve::procgen {

/**
 * Procedural generation module — runtime map generation (Phase A).
 * Script: `procgen <- eve.Procgen();`
 *
 * Grid cells use semantic ids; map to tile GIDs via palettes when applying
 * to TileLayer.
 */
class Procgen : public Module {
public:
    Module_REG(Procgen);
    Procgen();
    ~Procgen() override = default;

    Params     *newParams();
    OutputSpec *newOutput();
    Grid2D     *newGrid(int width, int height);

    Grid2D *generate(const std::string &algorithmId, Params *params);
    bool    generateTo(const std::string &algorithmId, Params *params, OutputSpec *output);
    bool    applyToLayer(Grid2D *grid, const std::string &palette, map::TileLayer *layer);

    void setPaletteGid(const std::string &palette, const std::string &semantic, int gid);
    int  getPaletteGid(const std::string &palette, const std::string &semantic) const;

    int         getAlgorithmCount() const;
    std::string getAlgorithmId(int index) const;
    bool        hasAlgorithm(const std::string &algorithmId) const;

    std::string lastError() const;
    std::string gridToJson(Grid2D *grid) const;

    PaletteTable &palettes() { return palettes_; }

private:
    bool runGenerate(const std::string &algorithmId, const Params &params, Grid2D &out);

    PaletteTable             palettes_;
    mutable std::string      lastError_;
    mutable std::vector<std::string> algorithmIdsCache_;
};

}  // namespace eve::procgen
