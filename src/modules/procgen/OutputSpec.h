#pragma once
#include "common/Export.h"


#include <string>

namespace eve::map {
class TileLayer;
}

namespace eve::procgen {

/**
 * @brief Configurable generation sink.
 * target: "grid" | "tilelayer" | "json"
 */
class EVENGINE_API_DOMAINS OutputSpec {
public:
    /** @brief Sets the target. */
    void        setTarget(const std::string &target);
    /** @brief Returns the target. */
    std::string getTarget() const;

    /** @brief Sets the layer. */
    void             setLayer(map::TileLayer *layer);
    /** @brief Returns the layer. */
    map::TileLayer * getLayer() const;

    /** @brief Sets the palette. */
    void        setPalette(const std::string &paletteName);
    /** @brief Returns the palette. */
    std::string getPalette() const;

    /** @brief Sets the path. */
    void        setPath(const std::string &path);
    /** @brief Returns the path. */
    std::string getPath() const;

private:
    std::string     target_  = "grid";
    map::TileLayer *layer_   = nullptr;
    std::string     palette_ = "default";
    std::string     path_;
};

}  // namespace eve::procgen
