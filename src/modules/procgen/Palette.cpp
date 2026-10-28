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

}  // namespace eve::procgen
