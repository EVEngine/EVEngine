#pragma once

#include "common/Module.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Texture;
}

namespace eve::filesystem {

/**
 * Lightweight path→reload registry for soft hot reload.
 * Driven from load.nut via pollWatch → tryReload; also used by watchTree.
 */
class HotReload : public Module {
public:
    Module_REG(HotReload);

    HotReload() = default;
    ~HotReload() override = default;

    /** Register a path for explicit reload (kind: "auto"|"particle"|"tilemap"|"texture"). */
    void bind(std::string path, std::string kind = "auto");
    void unbind(std::string path);

    /**
     * Reload assets matching path (normalized).
     * .json → particle emitters / tilemap layers with Resource.path
     * images → Graphics path-cached texture + emitters/layers with texturePath
     * Returns true if anything reloaded.
     */
    bool tryReload(std::string path);

    /** Recursively watch root and all subdirectories. Returns number of watches added. */
    int watchTree(std::string root = ".");

    static std::string normalizePath(std::string path);

private:
    bool reloadParticles(const std::string &normPath);
    bool reloadTilemaps(const std::string &normPath);
    bool reloadTextures(const std::string &normPath);
    bool isImagePath(const std::string &normPath) const;
    bool isJsonPath(const std::string &normPath) const;

    std::unordered_map<std::string, std::string> bindings_;  // norm path → kind
};

}  // namespace eve::filesystem
