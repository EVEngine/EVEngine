#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace eve::graphics {

/** Mag/min filter for texture sampling. */
enum class FilterMode {
    Nearest,
    Linear,
};

/** Mipmap filter; None disables mip sampling (maxLod clamped to 0). */
enum class MipmapMode {
    None,
    Nearest,
    Linear,
};

/**
 * Sampler state for a Texture (filter, wrap, mip LOD, anisotropy).
 * Defaults match historical engine behaviour: linear, clamp, no mips, no aniso.
 */
struct TextureSampler {
    FilterMode min = FilterMode::Linear;
    FilterMode mag = FilterMode::Linear;
    MipmapMode mipmap = MipmapMode::None;
    bool repeatU = false;
    bool repeatV = false;
    bool repeatW = false;
    /** 1 = off; values >1 enable anisotropic filtering when the device supports it. */
    float maxAnisotropy = 1.f;
    float lodBias = 0.f;
    float minLod = 0.f;
    /** Inclusive upper LOD clamp. Large values mean "use all available mips". */
    float maxLod = 1000.f;

    static TextureSampler nearest() {
        TextureSampler s;
        s.min = FilterMode::Nearest;
        s.mag = FilterMode::Nearest;
        s.mipmap = MipmapMode::None;
        return s;
    }

    static TextureSampler linear() {
        TextureSampler s;
        s.min = FilterMode::Linear;
        s.mag = FilterMode::Linear;
        s.mipmap = MipmapMode::None;
        return s;
    }

    /** Trilinear (linear + linear mips). Caller should create the texture with generateMipmaps. */
    static TextureSampler linearMipmap() {
        TextureSampler s;
        s.min = FilterMode::Linear;
        s.mag = FilterMode::Linear;
        s.mipmap = MipmapMode::Linear;
        return s;
    }

    /** Trilinear + anisotropic filtering (capped by device limits at bind time). */
    static TextureSampler anisotropic(float maxAniso = 16.f) {
        TextureSampler s = linearMipmap();
        s.maxAnisotropy = maxAniso;
        return s;
    }

    static FilterMode parseFilter(const std::string &name) {
        if (name == "nearest" || name == "Nearest" || name == "NEAREST") return FilterMode::Nearest;
        return FilterMode::Linear;
    }

    static MipmapMode parseMipmap(const std::string &name) {
        if (name == "none" || name == "None" || name == "NONE" || name.empty()) return MipmapMode::None;
        if (name == "nearest" || name == "Nearest" || name == "NEAREST") return MipmapMode::Nearest;
        return MipmapMode::Linear;
    }

    static const char *filterName(FilterMode m) {
        return m == FilterMode::Nearest ? "nearest" : "linear";
    }

    static const char *mipmapName(MipmapMode m) {
        switch (m) {
        case MipmapMode::None:
            return "none";
        case MipmapMode::Nearest:
            return "nearest";
        default:
            return "linear";
        }
    }
};

/**
 * Options for Graphics::newTexture / newCubemap.
 * When generateMipmaps is true and sampler.mipmap is None, mipmap mode becomes Linear.
 */
struct TextureCreateInfo {
    TextureSampler sampler{};
    bool generateMipmaps = false;

    static TextureCreateInfo defaults() { return {}; }

    static TextureCreateInfo withMipmaps(bool aniso = true, float maxAniso = 16.f) {
        TextureCreateInfo info;
        info.generateMipmaps = true;
        info.sampler = aniso ? TextureSampler::anisotropic(maxAniso) : TextureSampler::linearMipmap();
        return info;
    }
};

/** Full mip chain count for a 2D image (including base level). */
inline int mipmapCountForSize(int width, int height) {
    if (width <= 0 || height <= 0) return 1;
    const int m = std::max(width, height);
    return static_cast<int>(std::floor(std::log2(static_cast<double>(m)))) + 1;
}

}  // namespace eve::graphics
