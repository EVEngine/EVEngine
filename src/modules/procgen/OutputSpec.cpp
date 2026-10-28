#include "procgen/OutputSpec.h"

#include "map/TileLayer.h"

namespace eve::procgen {

void OutputSpec::setTarget(const std::string &target) { target_ = target; }
std::string OutputSpec::getTarget() const { return target_; }

void OutputSpec::setLayer(map::TileLayer *layer) { layer_ = layer; }
map::TileLayer *OutputSpec::getLayer() const { return layer_; }

void OutputSpec::setPalette(const std::string &paletteName) { palette_ = paletteName; }
std::string OutputSpec::getPalette() const { return palette_; }

void OutputSpec::setPath(const std::string &path) { path_ = path; }
std::string OutputSpec::getPath() const { return path_; }

}  // namespace eve::procgen
