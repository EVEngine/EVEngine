#include "procgen/texture/PrototypeTextures.h"

#include "image/ImageData.h"
#include "procgen/ParamSchema.h"
#include "procgen/texture/TextureRecipe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace eve::procgen {
namespace {

constexpr std::array<PrototypeTextureDescriptor, 13> kPatterns{{
    {"labeled-grid", "Labeled Grid"},
    {"quadrant-grid", "Quadrant Grid"},
    {"fine-grid", "Fine Grid"},
    {"panel-grid", "Panel Grid"},
    {"diagonal-grid", "Diagonal Grid"},
    {"diagonal-fine", "Fine Diagonal Grid"},
    {"checker-fine", "Fine Checker"},
    {"checker-coarse", "Coarse Checker"},
    {"subtle-grid", "Subtle Grid"},
    {"stairs-guide", "Stairs Guide"},
    {"doorway-guide", "Doorway Guide"},
    {"window-guide", "Window Guide"},
    {"cross-markers", "Cross Markers"},
}};

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a = 255;
};

struct Canvas {
    int      width;
    int      height;
    uint8_t* pixels;

    void blend(int x, int y, Color color, float opacity) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        opacity            = std::clamp(opacity * (float(color.a) / 255.0f), 0.0f, 1.0f);
        const size_t index = (size_t(y) * size_t(width) + size_t(x)) * 4u;
        pixels[index + 0] =
            uint8_t(std::lround(float(pixels[index + 0]) * (1.0f - opacity) + float(color.r) * opacity));
        pixels[index + 1] =
            uint8_t(std::lround(float(pixels[index + 1]) * (1.0f - opacity) + float(color.g) * opacity));
        pixels[index + 2] =
            uint8_t(std::lround(float(pixels[index + 2]) * (1.0f - opacity) + float(color.b) * opacity));
        pixels[index + 3] = 255;
    }

    void fill(Color color) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const size_t index = (size_t(y) * size_t(width) + size_t(x)) * 4u;
                pixels[index + 0]  = color.r;
                pixels[index + 1]  = color.g;
                pixels[index + 2]  = color.b;
                pixels[index + 3]  = 255;
            }
    }
};

bool hasPattern(std::string_view id) {
    return std::any_of(kPatterns.begin(), kPatterns.end(), [id](const auto& pattern) { return pattern.id == id; });
}

bool hasPalette(std::string_view palette) {
    constexpr std::array<std::string_view, 7> kPalettes{{"dark", "light", "purple", "orange", "green", "red",
                                                         "custom"}};
    return std::find(kPalettes.begin(), kPalettes.end(), palette) != kPalettes.end();
}

Color paletteColor(const Params& params, std::string_view palette) {
    if (palette == "light") return {207, 214, 222};
    if (palette == "purple") return {140, 100, 190};
    if (palette == "orange") return {215, 135, 65};
    if (palette == "green") return {70, 170, 115};
    if (palette == "red") return {205, 82, 90};
    if (palette == "custom") {
        return {uint8_t(std::clamp(params.getInt("backgroundR", 51), 0, 255)),
                uint8_t(std::clamp(params.getInt("backgroundG", 51), 0, 255)),
                uint8_t(std::clamp(params.getInt("backgroundB", 53), 0, 255))};
    }
    return {82, 92, 106};
}

Color lineColor(const Params& params, std::string_view palette) {
    if (palette == "light") return {71, 82, 95};
    if (palette == "purple") return {220, 201, 239};
    if (palette == "orange") return {247, 215, 174};
    if (palette == "green") return {190, 231, 207};
    if (palette == "red") return {241, 198, 201};
    if (palette == "dark") return {166, 180, 197};
    return {uint8_t(std::clamp(params.getInt("lineR", 255), 0, 255)),
            uint8_t(std::clamp(params.getInt("lineG", 255), 0, 255)),
            uint8_t(std::clamp(params.getInt("lineB", 255), 0, 255))};
}

void line(Canvas& canvas, int x0, int y0, int x1, int y1, Color color, float alpha, int width) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int       error = dx + dy;
    for (;;) {
        const int radius = std::max(0, width - 1) / 2;
        for (int oy = -radius; oy <= radius; ++oy)
            for (int ox = -radius; ox <= radius; ++ox) canvas.blend(x0 + ox, y0 + oy, color, alpha);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void rectangle(Canvas& canvas, int x0, int y0, int x1, int y1, Color color, float alpha, int width) {
    line(canvas, x0, y0, x1, y0, color, alpha, width);
    line(canvas, x1, y0, x1, y1, color, alpha, width);
    line(canvas, x1, y1, x0, y1, color, alpha, width);
    line(canvas, x0, y1, x0, y0, color, alpha, width);
}

void grid(Canvas& canvas, int cell, int lineWidth, Color color, float minorAlpha, float majorAlpha, int majorEvery,
          bool diagonals) {
    cell = std::max(2, cell);
    for (int x = 0; x < canvas.width; x += cell) {
        const bool major = (x / cell) % std::max(1, majorEvery) == 0;
        line(canvas, x, 0, x, canvas.height - 1, color, major ? majorAlpha : minorAlpha, lineWidth);
    }
    for (int y = 0; y < canvas.height; y += cell) {
        const bool major = (y / cell) % std::max(1, majorEvery) == 0;
        line(canvas, 0, y, canvas.width - 1, y, color, major ? majorAlpha : minorAlpha, lineWidth);
    }
    if (diagonals) {
        for (int y = -canvas.height; y < canvas.height * 2; y += cell)
            line(canvas, 0, y, canvas.width - 1, y + canvas.width - 1, color, minorAlpha, lineWidth);
        for (int y = 0; y < canvas.height * 3; y += cell)
            line(canvas, 0, y, canvas.width - 1, y - canvas.width + 1, color, minorAlpha, lineWidth);
    }
}

void checker(Canvas& canvas, int cell, Color lineColor, float alpha) {
    for (int y = 0; y < canvas.height; ++y)
        for (int x = 0; x < canvas.width; ++x) {
            if (((x / cell) + (y / cell)) % 2 == 0) canvas.blend(x, y, lineColor, alpha);
        }
}

void cross(Canvas& canvas, int cx, int cy, int radius, Color color, float alpha, int width) {
    line(canvas, cx - radius, cy, cx + radius, cy, color, alpha, width);
    line(canvas, cx, cy - radius, cx, cy + radius, color, alpha, width);
}

void paintPattern(std::string_view id, Canvas& canvas, const Params& params, Color foreground) {
    const int   cell       = std::clamp(params.getInt("cellSize", std::max(8, canvas.width / 8)), 2, 4096);
    const int   lineWidth  = std::clamp(params.getInt("lineWidth", std::max(1, canvas.width / 512)), 1, 64);
    const float minorAlpha = std::clamp(params.getFloat("minorAlpha", 0.10f), 0.0f, 1.0f);
    const float majorAlpha = std::clamp(params.getFloat("majorAlpha", 0.45f), 0.0f, 1.0f);
    if (id == "labeled-grid" || id == "quadrant-grid") {
        grid(canvas, std::max(2, cell / 2), lineWidth, foreground, minorAlpha, majorAlpha, 4, false);
        if (id == "labeled-grid") {
            const int pad = std::max(4, cell / 4);
            for (int i = 0; i < 4; ++i)
                line(canvas, pad, pad + i * lineWidth * 3, pad + cell / (i + 2), pad + i * lineWidth * 3, foreground,
                     0.95f, lineWidth);
        }
    } else if (id == "fine-grid") {
        grid(canvas, std::max(2, cell / 2), lineWidth, foreground, minorAlpha, majorAlpha * 0.5f, 4, false);
    } else if (id == "panel-grid") {
        grid(canvas, cell * 2, lineWidth, foreground, minorAlpha, majorAlpha, 1, false);
    } else if (id == "diagonal-grid" || id == "diagonal-fine") {
        const int scale = id == "diagonal-fine" ? std::max(2, cell / 2) : cell;
        grid(canvas, scale, lineWidth, foreground, minorAlpha, majorAlpha, 4, true);
    } else if (id == "checker-fine" || id == "checker-coarse") {
        checker(canvas, id == "checker-fine" ? std::max(2, cell / 2) : cell * 2, foreground, minorAlpha);
    } else if (id == "subtle-grid") {
        grid(canvas, cell * 2, lineWidth, foreground, minorAlpha * 0.55f, majorAlpha * 0.25f, 4, false);
    } else if (id == "stairs-guide") {
        grid(canvas, cell, lineWidth, foreground, minorAlpha, majorAlpha * 0.4f, 4, false);
        const int steps = std::clamp(params.getInt("guideSteps", 6), 2, 32);
        int       x = canvas.width / 4, y = canvas.height * 3 / 4;
        const int dx = canvas.width / 2 / steps, dy = canvas.height / 2 / steps;
        for (int i = 0; i < steps; ++i) {
            line(canvas, x, y, x + dx, y, foreground, 0.95f, lineWidth * 2);
            x += dx;
            line(canvas, x, y, x, y - dy, foreground, 0.95f, lineWidth * 2);
            y -= dy;
        }
    } else if (id == "doorway-guide") {
        grid(canvas, cell, lineWidth, foreground, minorAlpha, majorAlpha * 0.35f, 4, false);
        rectangle(canvas, canvas.width / 4, canvas.height / 4, canvas.width * 3 / 4, canvas.height, foreground, 0.95f,
                  lineWidth * 2);
        rectangle(canvas, canvas.width / 4 + cell / 3, canvas.height / 4 + cell / 3, canvas.width * 3 / 4 - cell / 3,
                  canvas.height, foreground, 0.55f, lineWidth);
    } else if (id == "window-guide") {
        grid(canvas, cell, lineWidth, foreground, minorAlpha, majorAlpha * 0.35f, 4, false);
        rectangle(canvas, canvas.width / 4, canvas.height / 4, canvas.width * 3 / 4, canvas.height * 3 / 4, foreground,
                  0.95f, lineWidth * 2);
        rectangle(canvas, canvas.width / 4 + cell / 3, canvas.height / 4 + cell / 3, canvas.width * 3 / 4 - cell / 3,
                  canvas.height * 3 / 4 - cell / 3, foreground, 0.55f, lineWidth);
    } else if (id == "cross-markers") {
        const int radius = std::max(2, cell / 5);
        for (int y = cell / 2; y < canvas.height; y += cell * 2)
            for (int x = cell / 2; x < canvas.width; x += cell * 2)
                cross(canvas, x, y, radius, foreground, majorAlpha, lineWidth);
    }
}

RecipeDescriptor descriptorFor(const PrototypeTextureDescriptor& pattern) {
    RecipeDescriptor schema =
        RecipeDescriptor::grid("tex.prototype." + std::string(pattern.id), std::string(pattern.displayName),
                               "Prototype Texture", 8, 8, 4096, 4096);
    schema.params.push_back(ParamDescriptor::choice("palette", "Palette", "dark",
                                                    {"dark", "light", "purple", "orange", "green", "red", "custom"}));
    schema.params.push_back(ParamDescriptor::integer("cellSize", "Cell Size", 128, 2, 4096));
    schema.params.push_back(ParamDescriptor::integer("lineWidth", "Line Width", 2, 1, 64));
    schema.params.push_back(ParamDescriptor::floating("minorAlpha", "Minor Line Opacity", 0.10f, 0.0f, 1.0f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("majorAlpha", "Major Line Opacity", 0.45f, 0.0f, 1.0f, 0.01f));
    schema.params.push_back(ParamDescriptor::integer("guideSteps", "Guide Steps", 6, 2, 32));
    schema.params.push_back(ParamDescriptor::integer("backgroundR", "Custom Background R", 51, 0, 255));
    schema.params.push_back(ParamDescriptor::integer("backgroundG", "Custom Background G", 51, 0, 255));
    schema.params.push_back(ParamDescriptor::integer("backgroundB", "Custom Background B", 53, 0, 255));
    schema.params.push_back(ParamDescriptor::integer("lineR", "Custom Line R", 255, 0, 255));
    schema.params.push_back(ParamDescriptor::integer("lineG", "Custom Line G", 255, 0, 255));
    schema.params.push_back(ParamDescriptor::integer("lineB", "Custom Line B", 255, 0, 255));
    return schema;
}

}  // namespace

std::span<const PrototypeTextureDescriptor> prototypeTextureDescriptors() noexcept { return kPatterns; }

eve::Result<std::unique_ptr<image::ImageData>> generatePrototypeTexture(std::string_view patternId,
                                                                        const Params&    params) {
    if (!hasPattern(patternId)) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "unknown prototype texture pattern", "patternId"));
    }
    const int width  = params.getWidth();
    const int height = params.getHeight();
    if (width < 8 || height < 8 || width > 4096 || height > 4096) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "prototype texture dimensions must be between 8 and 4096", "params.size"));
    }
    const std::string palette = params.getString("palette", "dark");
    if (!hasPalette(palette)) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown prototype texture palette",
                                   "params.palette"));
    }
    const float invalidFloat = std::numeric_limits<float>::quiet_NaN();
    const float minorAlpha =
        params.has("minorAlpha") ? params.getFloat("minorAlpha", invalidFloat) : 0.10f;
    const float majorAlpha =
        params.has("majorAlpha") ? params.getFloat("majorAlpha", invalidFloat) : 0.45f;
    if (!std::isfinite(minorAlpha) || !std::isfinite(majorAlpha)) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "prototype texture opacities must be finite", "params.opacity"));
    }
    auto  image  = std::make_unique<image::ImageData>(width, height, "RGBA8");
    auto* pixels = static_cast<uint8_t*>(image->getData());
    if (!pixels) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "prototype texture allocation failed", "image"));
    }
    Canvas canvas{width, height, pixels};
    canvas.fill(paletteColor(params, palette));
    paintPattern(patternId, canvas, params, lineColor(params, palette));
    return eve::Result<std::unique_ptr<image::ImageData>>::success(std::move(image),
                                                                   eve::Status::success(eve::StatusCode::Applied));
}

void registerPrototypeTextureRecipes(TextureRecipeRegistry& registry) {
    for (const auto& pattern : kPatterns) {
        const std::string id(pattern.id);
        registry.registerRecipe(descriptorFor(pattern), [id](const Params& params, std::string& error) {
            auto generated = generatePrototypeTexture(id, params);
            if (!generated.ok()) {
                error = generated.status().describe();
                return std::unique_ptr<image::ImageData>{};
            }
            return std::move(generated).takeValue();
        });
    }
}

}  // namespace eve::procgen
