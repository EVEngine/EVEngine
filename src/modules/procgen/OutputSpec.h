#pragma once

#include "map/TileLayer.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Configurable generation sink.
 * target: "grid" | "tilelayer" | "json"
 */
class OutputSpec {
public:
    void        setTarget(const std::string &target);
    std::string getTarget() const;

    void             setLayer(map::TileLayer *layer);
    map::TileLayer * getLayer() const;

    void        setPalette(const std::string &paletteName);
    std::string getPalette() const;

    void        setPath(const std::string &path);
    std::string getPath() const;

private:
    std::string     target_  = "grid";
    map::TileLayer *layer_   = nullptr;
    std::string     palette_ = "default";
    std::string     path_;
};

}  // namespace eve::procgen
