#include "procgen/Palette.h"

#include "procgen/Semantic.h"

namespace eve::procgen {

void PaletteTable::setGid(const std::string &palette, const std::string &semantic, int gid) {
    palettes_[palette][semantic] = gid;
}

int PaletteTable::getGid(const std::string &palette, const std::string &semantic) const {
    auto pit = palettes_.find(palette);
    if (pit == palettes_.end()) return 0;
    auto sit = pit->second.find(semantic);
    return sit == pit->second.end() ? 0 : sit->second;
}

int PaletteTable::getGid(const std::string &palette, uint32_t sid) const {
    return getGid(palette, semanticName(sid));
}

bool PaletteTable::applyToLayer(const Grid2D &grid, const std::string &palette,
                                map::TileLayer *layer, std::string *error) const {
    if (!layer) {
        if (error) *error = "applyToLayer: null TileLayer";
        return false;
    }
    const int w = grid.getWidth();
    const int h = grid.getHeight();
    if (w <= 0 || h <= 0) {
        if (error) *error = "applyToLayer: empty grid";
        return false;
    }
    if (layer->getMapWidth() != w || layer->getMapHeight() != h) {
        layer->resize(w, h);
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            layer->setTile(x, y, getGid(palette, grid.getCell(x, y)));
        }
    }
    return true;
}

}  // namespace eve::procgen
