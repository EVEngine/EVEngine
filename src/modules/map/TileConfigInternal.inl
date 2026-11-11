#pragma once
// Internal map admission seam; candidate entity pointers never escape map publication code.
#include "map/TileConfig.h"
namespace eve::service {
class IFileSystem;
}
namespace eve::map::detail {
// Same canonical parser with an injected read service and no persistent watches.
std::vector<TileLayer*> prepareMapAssetText(const std::string& json, const std::string& path,
                                            service::IFileSystem& files, std::vector<MapObject>* objects,
                                            std::string* error);
}  // namespace eve::map::detail
