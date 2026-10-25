#pragma once

#include "common/Export.h"

namespace eve::graphics {

/** @brief Mag/min filter for texture sampling. */
enum class FilterMode {
    Nearest,
    Linear,
};

/** @brief Mipmap filter; Disabled turns off mip sampling (maxLod clamped to 0). */
enum class MipmapMode {
    Disabled,
    Nearest,
    Linear,
};

/**
 * @brief Sampler state for a Texture (filter, wrap, mip LOD, anisotropy).
 * Defaults match historical engine behaviour: linear, clamp, no mips, no aniso.
 */
struct TextureSampler {
    FilterMode min     = FilterMode::Linear;
    FilterMode mag     = FilterMode::Linear;
    MipmapMode mipmap  = MipmapMode::Disabled;
    bool       repeatU = false;
    bool       repeatV = false;
    bool       repeatW = false;
    /** @brief 1 = off; values >1 enable anisotropic filtering when the device supports it. */
    float maxAnisotropy = 1.f;
    float lodBias       = 0.f;
    float minLod        = 0.f;
    /** @brief Inclusive upper LOD clamp. Large values mean "use all available mips". */
    float maxLod = 1000.f;

    /** @brief Compare requested state without creating a backend sampler. */
    bool operator==(const TextureSampler&) const = default;

    /** @brief Nearest. */
    static TextureSampler nearest() {
        TextureSampler s;
        s.min    = FilterMode::Nearest;
        s.mag    = FilterMode::Nearest;
        s.mipmap = MipmapMode::Disabled;
        return s;
    }

    /** @brief Linear. */
    static TextureSampler linear() {
        TextureSampler s;
        s.min    = FilterMode::Linear;
        s.mag    = FilterMode::Linear;
        s.mipmap = MipmapMode::Disabled;
        return s;
    }

    /** @brief Trilinear (linear + linear mips). Caller should create the texture with generateMipmaps. */
    static TextureSampler linearMipmap() {
        TextureSampler s;
        s.min    = FilterMode::Linear;
        s.mag    = FilterMode::Linear;
        s.mipmap = MipmapMode::Linear;
        return s;
    }

    /** @brief Trilinear + anisotropic filtering (capped by device limits at bind time). */
    static TextureSampler anisotropic(float maxAniso = 16.f) {
        TextureSampler s = linearMipmap();
        s.maxAnisotropy  = maxAniso;
        return s;
    }

    /**
     * @brief Parse a filter name (`nearest` / `linear`, case-insensitive).
     * @param name Borrowed NUL-terminated spelling; nullptr treated as linear.
     */
    EVENGINE_API_BACKENDS static FilterMode parseFilter(const char* name);
    /**
     * @brief Parse a mipmap name (`none` / `nearest` / `linear`, case-insensitive).
     * @param name Borrowed NUL-terminated spelling; nullptr/empty means Disabled.
     */
    EVENGINE_API_BACKENDS static MipmapMode parseMipmap(const char* name);

    /** @brief Filter name. */
    static const char* filterName(FilterMode m) { return m == FilterMode::Nearest ? "nearest" : "linear"; }

    /** @brief Mipmap name. */
    static const char* mipmapName(MipmapMode m) {
        switch (m) {
            case MipmapMode::Disabled: return "none";
            case MipmapMode::Nearest: return "nearest";
            default: return "linear";
        }
    }
};

/**
 * @brief Options for Graphics::newTexture / newCubemap.
 * When generateMipmaps is true and sampler.mipmap is Disabled, mipmap mode becomes Linear.
 */
struct TextureCreateInfo {
    TextureSampler sampler{};
    bool           generateMipmaps = false;

    /** @brief Defaults. */
    static TextureCreateInfo defaults() { return {}; }

    /** @brief With mipmaps. */
    static TextureCreateInfo withMipmaps(bool aniso = true, float maxAniso = 16.f) {
        TextureCreateInfo info;
        info.generateMipmaps = true;
        info.sampler         = aniso ? TextureSampler::anisotropic(maxAniso) : TextureSampler::linearMipmap();
        return info;
    }
};

/**
 * @brief Full mip chain count for a 2D image (including base level).
 * @cost O(1); uses integer log2 of the larger dimension.
 */
EVENGINE_API_BACKENDS int mipmapCountForSize(int width, int height);

}  // namespace eve::graphics
