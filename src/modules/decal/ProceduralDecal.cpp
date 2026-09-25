#include "decal/ProceduralDecal.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::decal {
namespace {

Result<ProceduralDecalRecipe> recipeFailure(std::string message) {
    return Result<ProceduralDecalRecipe>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "decal.procedural.preset"));
}

Result<ProceduralDecalBake> bakeFailure(std::string message) {
    return Result<ProceduralDecalBake>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "decal.procedural.recipe"));
}

std::optional<std::string_view> attribute(std::string_view element, std::string_view name) {
    const std::string needle = std::string(name) + "=\"";
    const auto begin = element.find(needle);
    if (begin == std::string_view::npos) return std::nullopt;
    const auto valueBegin = begin + needle.size();
    const auto valueEnd = element.find('"', valueBegin);
    if (valueEnd == std::string_view::npos) return std::nullopt;
    return element.substr(valueBegin, valueEnd - valueBegin);
}

std::optional<float> number(std::string_view text) {
    float value = 0.f;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::optional<int> integer(std::string_view text) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

std::optional<std::array<float, 3>> color3(std::string_view text) {
    std::array<float, 3> color{};
    for (std::size_t channel = 0; channel < color.size(); ++channel) {
        const auto comma = text.find(',');
        const auto token = comma == std::string_view::npos ? text : text.substr(0, comma);
        auto value = number(token);
        if (!value) return std::nullopt;
        color[channel] = *value;
        if (channel + 1 < color.size()) {
            if (comma == std::string_view::npos) return std::nullopt;
            text.remove_prefix(comma + 1);
        } else if (comma != std::string_view::npos) {
            return std::nullopt;
        }
    }
    return color;
}

DecalPattern importedPattern(int sourceId) {
    // Substance exposes a larger pattern catalogue. Preserve deterministic variation while mapping it
    // onto the five native pattern families rather than depending on Substance at runtime.
    constexpr DecalPattern families[] = {DecalPattern::Spots, DecalPattern::Cracks, DecalPattern::Streaks,
                                         DecalPattern::Puddle, DecalPattern::Grunge};
    const auto magnitude = sourceId < 0 ? std::uint64_t(-std::int64_t(sourceId)) : std::uint64_t(sourceId);
    return families[std::size_t(magnitude % std::size(families))];
}

float clamp01(float value) { return std::clamp(value, 0.f, 1.f); }

float smoothStep(float edge0, float edge1, float value) {
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.f - 2.f * t);
}

std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float random01(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
    return float(hash(x * 0x9e3779b9u ^ y * 0x85ebca6bu ^ seed)) / float(std::numeric_limits<std::uint32_t>::max());
}

float valueNoise(float x, float y, std::uint32_t seed) {
    const int   ix     = static_cast<int>(std::floor(x));
    const int   iy     = static_cast<int>(std::floor(y));
    const float fx     = x - float(ix);
    const float fy     = y - float(iy);
    const float sx     = fx * fx * (3.f - 2.f * fx);
    const float sy     = fy * fy * (3.f - 2.f * fy);
    const auto  sample = [seed](int px, int py) {
        return random01(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py), seed);
    };
    const float a = std::lerp(sample(ix, iy), sample(ix + 1, iy), sx);
    const float b = std::lerp(sample(ix, iy + 1), sample(ix + 1, iy + 1), sx);
    return std::lerp(a, b, sy);
}

float fractal(float x, float y, std::uint32_t seed) {
    float value  = 0.f;
    float weight = 0.55f;
    for (int octave = 0; octave < 4; ++octave) {
        value += valueNoise(x, y, seed + std::uint32_t(octave) * 1013u) * weight;
        x *= 2.03f;
        y *= 2.03f;
        weight *= 0.5f;
    }
    return value / 1.03125f;
}

float patternValue(DecalPattern pattern, int sourcePatternId, float u, float v, float scale,
                   std::uint32_t seed) {
    const float x      = (u - 0.5f) * scale;
    const float y      = (v - 0.5f) * scale;
    const float radius = std::sqrt(x * x + y * y);
    const float noise  = fractal(x + 17.1f, y - 9.7f, seed);
    const int family = sourcePatternId >= 0 ? sourcePatternId % 12 : static_cast<int>(pattern);
    switch (family) {
        case 0: {
            const float cells = valueNoise(x * 2.1f, y * 2.1f, seed + 71u);
            return clamp01((cells - 0.42f) * 2.8f) * clamp01(1.15f - radius * 0.55f);
        }
        case 1: {
            const float ridge = 1.f - std::abs(noise * 2.f - 1.f);
            return clamp01((ridge - 0.76f) * 5.2f) * clamp01(1.2f - radius * 0.45f);
        }
        case 2: {
            const float streak = fractal(x * 0.45f, y * 3.8f, seed + 191u);
            return clamp01((streak - 0.38f) * 2.1f) * clamp01(1.1f - std::abs(x) * 0.22f);
        }
        case 3: {
            const float warpedRadius = radius * (0.78f + 0.42f * noise);
            return 1.f - smoothStep(0.62f, 1.05f, warpedRadius);
        }
        case 4: return clamp01((noise - 0.28f) * 1.45f) * clamp01(1.25f - radius * 0.38f);
        case 5: {
            const float islands = fractal(x * 1.45f + noise * 0.55f, y * 1.45f - noise * 0.35f,
                                          seed + 257u);
            return clamp01((islands - 0.46f) * 2.65f) * clamp01(1.14f - radius * 0.48f);
        }
        case 6: {
            const float scratches = fractal((x + y) * 4.8f, (y - x) * 0.38f, seed + 313u);
            return clamp01((scratches - 0.56f) * 3.2f) * clamp01(1.18f - radius * 0.42f);
        }
        case 7: {
            const float blobs = valueNoise(x * 1.35f, y * 1.35f, seed + 419u);
            const float drops = valueNoise(x * 4.7f, y * 4.7f, seed + 431u);
            return clamp01((std::max(blobs, drops * 0.82f) - 0.48f) * 2.7f) *
                   clamp01(1.15f - radius * 0.46f);
        }
        case 8: {
            const float cells = valueNoise(x * 2.6f, y * 2.6f, seed + 557u);
            const float edge = 1.f - std::abs(cells * 2.f - 1.f);
            return clamp01((edge - 0.68f) * 4.1f) * clamp01(1.2f - radius * 0.44f);
        }
        case 9: {
            const float smear = fractal(x * 2.8f + noise, y * 0.42f, seed + 631u);
            return clamp01((smear - 0.4f) * 2.25f) * clamp01(1.1f - radius * 0.48f);
        }
        case 10: {
            const float drops = valueNoise(x * 5.3f, y * 5.3f, seed + 743u);
            return clamp01((drops - 0.63f) * 4.4f) * clamp01(1.16f - radius * 0.4f);
        }
        case 11: {
            const float coarse = fractal(x * 0.72f, y * 0.72f, seed + 829u);
            const float fine = fractal(x * 3.1f, y * 3.1f, seed + 853u);
            return clamp01((coarse * 0.7f + fine * 0.3f - 0.38f) * 2.f) *
                   clamp01(1.22f - radius * 0.4f);
        }
    }
    return 0.f;
}

std::vector<float> layerMask(const ProceduralDecalRecipe& recipe, const ProceduralDecalLayer& layer) {
    const std::size_t  count = std::size_t(recipe.width) * std::size_t(recipe.height);
    std::vector<float> mask(count, 0.f);
    if (!layer.enabled) return mask;
    const float c = std::cos(layer.rotation);
    const float s = std::sin(layer.rotation);
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            const float u     = (float(x) + 0.5f) / float(recipe.width) - 0.5f;
            const float v     = (float(y) + 0.5f) / float(recipe.height) - 0.5f;
            const float ru    = c * u - s * v + 0.5f;
            const float rv    = s * u + c * v + 0.5f;
            float       value = patternValue(layer.pattern, layer.sourcePatternId, ru, rv, layer.scale,
                                             recipe.seed ^ layer.seed);
            if (layer.invertMask) value = 1.f - value;
            value             = clamp01((value - 0.5f) * layer.contrast + 0.5f);
            mask[std::size_t(y) * std::size_t(recipe.width) + std::size_t(x)] = clamp01(value * layer.amount);
        }
    }
    const int radius = std::clamp(static_cast<int>(std::round(layer.blur * 12.f)), 0, 12);
    if (radius == 0) return mask;
    std::vector<float> horizontal(count, 0.f);
    std::vector<float> result(count, 0.f);
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            float sum = 0.f;
            for (int dx = -radius; dx <= radius; ++dx)
                sum += mask[std::size_t(y) * std::size_t(recipe.width) +
                            std::size_t(std::clamp(x + dx, 0, recipe.width - 1))];
            horizontal[std::size_t(y) * std::size_t(recipe.width) + std::size_t(x)] = sum / float(radius * 2 + 1);
        }
    }
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            float sum = 0.f;
            for (int dy = -radius; dy <= radius; ++dy)
                sum += horizontal[std::size_t(std::clamp(y + dy, 0, recipe.height - 1)) * std::size_t(recipe.width) +
                                  std::size_t(x)];
            result[std::size_t(y) * std::size_t(recipe.width) + std::size_t(x)] = sum / float(radius * 2 + 1);
        }
    }
    return result;
}

std::uint8_t byte(float value) { return static_cast<std::uint8_t>(std::round(clamp01(value) * 255.f)); }

std::array<float, 3> rotateHue(const std::array<float, 3>& color, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {
        clamp01(color[0] * (0.299f + 0.701f * c + 0.168f * s) +
                color[1] * (0.587f - 0.587f * c + 0.330f * s) +
                color[2] * (0.114f - 0.114f * c - 0.497f * s)),
        clamp01(color[0] * (0.299f - 0.299f * c - 0.328f * s) +
                color[1] * (0.587f + 0.413f * c + 0.035f * s) +
                color[2] * (0.114f - 0.114f * c + 0.292f * s)),
        clamp01(color[0] * (0.299f - 0.300f * c + 1.250f * s) +
                color[1] * (0.587f - 0.588f * c - 1.050f * s) +
                color[2] * (0.114f + 0.886f * c - 0.203f * s)),
    };
}

bool finiteLayer(const ProceduralDecalLayer& layer) {
    const float values[] = {layer.amount,         layer.scale,     layer.rotation,           layer.blur,
                            layer.contrast,       layer.color[0],  layer.color[1],           layer.color[2],
                            layer.colorVariation, layer.hueVariation, layer.roughness, layer.roughnessVariation,
                            layer.metallic, layer.emissive, layer.normalStrength, layer.normalSoftness,
                            layer.normalTrim, layer.normalThickness, layer.height};
    return std::all_of(std::begin(values), std::end(values), [](float value) { return std::isfinite(value); });
}

bool validLayer(const ProceduralDecalLayer& layer) {
    const auto inRange = [](float value, float minimum, float maximum) {
        return value >= minimum && value <= maximum;
    };
    return finiteLayer(layer) && inRange(layer.amount, 0.f, 1.f) &&
           inRange(layer.scale, 0.001f, 4096.f) && std::abs(layer.rotation) <= 1000000.f &&
           inRange(layer.blur, 0.f, 1.f) && inRange(layer.contrast, 0.f, 64.f) &&
           std::all_of(layer.color.begin(), layer.color.end(),
                       [&](float value) { return inRange(value, 0.f, 1.f); }) &&
           inRange(layer.colorVariation, 0.f, 1.f) && inRange(layer.hueVariation, 0.f, 1.f) &&
           inRange(layer.roughness, 0.f, 1.f) && inRange(layer.roughnessVariation, 0.f, 1.f) &&
           inRange(layer.metallic, 0.f, 1.f) && inRange(layer.emissive, 0.f, 1.f) &&
           inRange(layer.normalStrength, 0.f, 64.f) && inRange(layer.normalSoftness, 0.005f, 1.f) &&
           inRange(layer.normalTrim, 0.f, 1.f) && inRange(layer.normalThickness, 0.1f, 16.f) &&
           layer.normalStyle >= 0 && layer.normalStyle <= 16 && inRange(layer.height, 0.f, 1.f);
}

ProceduralDecalLayer layer(DecalPattern pattern, std::array<float, 3> color, float roughness, float metallic,
                         float height, std::uint32_t seed) {
    ProceduralDecalLayer value;
    value.pattern   = pattern;
    value.color     = color;
    value.roughness = roughness;
    value.metallic  = metallic;
    value.height    = height;
    value.seed      = seed;
    return value;
}

}  // namespace

Result<ProceduralDecalRecipe> proceduralDecalPreset(std::string_view name, std::uint32_t seed) {
    ProceduralDecalRecipe recipe;
    recipe.seed          = seed;
    recipe.layerB.amount = 0.45f;
    recipe.layerB.scale  = 9.f;
    recipe.layerB.blur   = 0.04f;
    if (name == "blood-wet") {
        recipe.layerA = layer(DecalPattern::Puddle, {0.34f, 0.002f, 0.001f}, 0.04f, 0.f, 0.65f, 11u);
        recipe.layerB = layer(DecalPattern::Spots, {0.62f, 0.03f, 0.02f}, 0.12f, 0.f, 0.42f, 12u);
    } else if (name == "blood-dried") {
        recipe.layerA = layer(DecalPattern::Spots, {0.16f, 0.012f, 0.006f}, 0.82f, 0.f, 0.28f, 21u);
        recipe.layerB = layer(DecalPattern::Cracks, {0.07f, 0.004f, 0.002f}, 0.94f, 0.f, 0.36f, 22u);
    } else if (name == "damage") {
        recipe.layerA                = layer(DecalPattern::Cracks, {0.055f, 0.05f, 0.045f}, 0.88f, 0.f, 0.82f, 31u);
        recipe.layerA.normalStrength = 5.f;
        recipe.layerB                = layer(DecalPattern::Spots, {0.12f, 0.1f, 0.08f}, 0.75f, 0.f, 0.35f, 32u);
    } else if (name == "dirt") {
        recipe.layerA = layer(DecalPattern::Grunge, {0.22f, 0.14f, 0.07f}, 0.92f, 0.f, 0.38f, 41u);
        recipe.layerB = layer(DecalPattern::Streaks, {0.32f, 0.24f, 0.12f}, 0.86f, 0.f, 0.22f, 42u);
    } else if (name == "rust") {
        recipe.layerA = layer(DecalPattern::Grunge, {0.42f, 0.075f, 0.018f}, 0.91f, 0.55f, 0.5f, 51u);
        recipe.layerB = layer(DecalPattern::Spots, {0.12f, 0.025f, 0.008f}, 0.98f, 0.25f, 0.68f, 52u);
    } else if (name == "puddle") {
        recipe.layerA         = layer(DecalPattern::Puddle, {0.035f, 0.045f, 0.05f}, 0.025f, 0.f, 0.08f, 61u);
        recipe.layerB.enabled = false;
    } else if (name == "paint") {
        recipe.layerA = layer(DecalPattern::Spots, {0.12f, 0.32f, 0.8f}, 0.32f, 0.f, 0.42f, 71u);
        recipe.layerB = layer(DecalPattern::Cracks, {0.025f, 0.025f, 0.025f}, 0.9f, 0.f, 0.62f, 72u);
    } else if (name == "moss" || name == "mold" || name == "lichen") {
        const std::array<float, 3> base =
            name == "mold" ? std::array<float, 3>{0.08f, 0.11f, 0.075f} : std::array<float, 3>{0.12f, 0.25f, 0.06f};
        recipe.layerA = layer(DecalPattern::Grunge, base, 0.96f, 0.f, 0.65f, 81u);
        recipe.layerB = layer(DecalPattern::Spots, {0.24f, 0.36f, 0.09f}, 0.88f, 0.f, 0.32f, 82u);
    } else {
        return recipeFailure("Unknown procedural decal preset: " + std::string(name));
    }
    recipe.layerA.seed ^= seed;
    recipe.layerB.seed ^= seed * 1664525u + 1013904223u;
    return Result<ProceduralDecalRecipe>::success(std::move(recipe));
}

Result<ProceduralDecalRecipe> importProceduralDecalSbsprs(std::string_view xml) {
    constexpr std::size_t maxPresetBytes = 1024u * 1024u;
    if (xml.empty()) return recipeFailure("Substance preset document is empty");
    if (xml.size() > maxPresetBytes) return recipeFailure("Substance preset document exceeds the 1 MiB limit");
    if (xml.find("<sbspresets") == std::string_view::npos || xml.find("<sbspreset") == std::string_view::npos)
        return recipeFailure("Input is not a Substance preset document");

    std::unordered_map<std::string, std::string> inputs;
    std::size_t cursor = 0;
    while ((cursor = xml.find("<presetinput", cursor)) != std::string_view::npos) {
        const auto end = xml.find("/>", cursor);
        if (end == std::string_view::npos) return recipeFailure("Unterminated presetinput element");
        const auto element = xml.substr(cursor, end + 2 - cursor);
        const auto id = attribute(element, "identifier");
        const auto value = attribute(element, "value");
        if (!id || !value) return recipeFailure("presetinput requires identifier and value attributes");
        inputs.insert_or_assign(std::string(*id), std::string(*value));
        cursor = end + 2;
    }
    if (inputs.empty()) return recipeFailure("Substance preset contains no inputs");

    ProceduralDecalRecipe recipe;
    bool malformedKnownInput = false;
    const auto readFloat = [&](std::string_view name) -> std::optional<float> {
        const auto found = inputs.find(std::string(name));
        if (found == inputs.end()) return std::nullopt;
        auto value = number(found->second);
        malformedKnownInput = malformedKnownInput || !value.has_value();
        return value;
    };
    const auto readInt = [&](std::string_view name) -> std::optional<int> {
        const auto found = inputs.find(std::string(name));
        if (found == inputs.end()) return std::nullopt;
        auto value = integer(found->second);
        malformedKnownInput = malformedKnownInput || !value.has_value();
        return value;
    };
    const auto applyLayer = [&](std::string_view prefix, ProceduralDecalLayer& target) -> bool {
        const std::string key(prefix);
        if (const auto value = readFloat(key + "amount")) target.amount = clamp01(*value);
        if (const auto value = readInt(key + "pattern")) {
            target.sourcePatternId = *value;
            target.pattern = importedPattern(*value);
        }
        if (const auto value = readFloat(key + "tweak")) target.scale = 1.f + clamp01(*value) * 11.f;
        if (const auto value = readFloat(key + "tile")) target.scale *= std::clamp(*value, 0.25f, 8.f);
        if (const auto value = readInt(key + "mask_invert")) target.invertMask = *value != 0;
        if (const auto value = readFloat(key + "blur")) target.blur = std::max(0.f, *value) * 0.15f;
        if (const auto value = readFloat(key + "contrast")) target.contrast = 0.5f + std::max(0.f, *value) * 2.f;
        const auto colorIt = inputs.find(key + "color");
        if (colorIt != inputs.end()) {
            auto value = color3(colorIt->second);
            if (!value) return false;
            target.color = *value;
        }
        if (const auto value = readFloat(key + "color_value_variation"))
            target.colorVariation = clamp01(*value);
        if (const auto value = readFloat(key + "color_hue_variation")) target.hueVariation = clamp01(*value);
        if (const auto value = readFloat(key + "roughness")) target.roughness = clamp01(*value);
        if (const auto value = readFloat(key + "rough_variation"))
            target.roughnessVariation = clamp01(*value);
        if (const auto value = readFloat(key + "metallic")) target.metallic = clamp01(*value);
        if (const auto value = readFloat(key + "emissive")) target.emissive = clamp01(*value);
        if (const auto value = readFloat(key + "normal")) target.normalStrength = std::abs(*value);
        if (const auto value = readFloat(key + "normal_softness"))
            target.normalSoftness = std::clamp(*value, 0.005f, 1.f);
        if (const auto value = readFloat(key + "normal_trim")) target.normalTrim = clamp01(*value);
        if (const auto value = readFloat(key + "normal_thickness"))
            target.normalThickness = std::clamp(*value, 0.1f, 16.f);
        if (const auto value = readInt(key + "normal_style")) target.normalStyle = *value;
        if (const auto value = readFloat(key + "height")) target.height = clamp01(*value);
        if (const auto value = readFloat(key + "color_opacity")) target.amount *= clamp01(*value);
        return true;
    };

    if (const auto seed = readInt("$randomseed")) recipe.seed = static_cast<std::uint32_t>(*seed);
    const auto outputSize = inputs.find("$outputsize");
    if (outputSize != inputs.end()) {
        const auto comma = outputSize->second.find(',');
        if (comma == std::string::npos) return recipeFailure("$outputsize must contain width,height exponents");
        const auto x = integer(std::string_view(outputSize->second).substr(0, comma));
        const auto y = integer(std::string_view(outputSize->second).substr(comma + 1));
        if (!x || !y || *x < 0 || *y < 0 || *x > 12 || *y > 12)
            return recipeFailure("$outputsize exponents must be in [0, 12]");
        recipe.width = 1 << *x;
        recipe.height = 1 << *y;
    }
    if (!applyLayer("a_", recipe.layerA) || !applyLayer("b_", recipe.layerB))
        return recipeFailure("Substance preset contains an invalid color value");
    if (const auto value = readFloat("ab_height_masking"))
        recipe.layerHeightBlend = clamp01((*value + 1.f) * 0.5f);
    if (const auto value = readFloat("ab_balance")) recipe.layerBalance = clamp01(*value);
    if (const auto value = readInt("ab_blendmode")) recipe.layerBlendMode = *value;
    if (malformedKnownInput)
        return recipeFailure("Substance preset contains a malformed recognized numeric value");
    recipe.layerA.seed = recipe.seed ^ 0x9e3779b9u;
    recipe.layerB.seed = recipe.seed ^ 0x85ebca6bu;
    return Result<ProceduralDecalRecipe>::success(std::move(recipe));
}

Result<ProceduralDecalBake> bakeProceduralDecal(const ProceduralDecalRecipe& recipe) {
    if (recipe.schemaVersion != ProceduralDecalRecipe::kSchemaVersion)
        return bakeFailure("Unsupported procedural decal recipe schema version");
    if (recipe.width <= 0 || recipe.height <= 0 || recipe.width > 4096 || recipe.height > 4096)
        return bakeFailure("Procedural decal dimensions must be in [1, 4096]");
    if (!validLayer(recipe.layerA) || !validLayer(recipe.layerB) ||
        !std::isfinite(recipe.layerHeightBlend) || recipe.layerHeightBlend < 0.f ||
        recipe.layerHeightBlend > 1.f || !std::isfinite(recipe.layerBalance) ||
        recipe.layerBalance < 0.f || recipe.layerBalance > 1.f || recipe.layerBlendMode < 0 ||
        recipe.layerBlendMode > 2 || !std::isfinite(recipe.opacity) || recipe.opacity < 0.f ||
        recipe.opacity > 1.f)
        return bakeFailure("Procedural decal recipe contains values outside supported ranges");

    const auto        maskA  = layerMask(recipe, recipe.layerA);
    const auto        maskB  = layerMask(recipe, recipe.layerB);
    const std::size_t pixels = std::size_t(recipe.width) * std::size_t(recipe.height);
    ProceduralDecalBake bake;
    bake.width  = recipe.width;
    bake.height = recipe.height;
    bake.albedo.resize(pixels * 4u);
    bake.normal.resize(pixels * 4u);
    bake.params.resize(pixels * 4u);
    std::vector<float> height(pixels, 0.f);
    std::vector<float> normalHeight(pixels, 0.f);
    std::vector<float> coverage(pixels, 0.f);
    for (std::size_t index = 0; index < pixels; ++index) {
        const float a = maskA[index] * (2.f * (1.f - recipe.layerBalance));
        const float b = maskB[index] * (2.f * recipe.layerBalance);
        const float bWeight =
            b * clamp01(0.5f + (recipe.layerB.height - recipe.layerA.height) * recipe.layerHeightBlend);
        const float combined = recipe.layerBlendMode == 2 ? std::max(a, bWeight) : a + bWeight * (1.f - a);
        const float alpha = clamp01(combined) * recipe.opacity;
        const float mixB  = alpha > 1e-6f ? bWeight / std::max(a + bWeight, 1e-6f) : 0.f;
        const float variation =
            random01(static_cast<std::uint32_t>(index % std::size_t(recipe.width)),
                     static_cast<std::uint32_t>(index / std::size_t(recipe.width)), recipe.seed + 991u) *
                2.f -
            1.f;
        const float hueNoise = random01(static_cast<std::uint32_t>(index % std::size_t(recipe.width)),
                                        static_cast<std::uint32_t>(index / std::size_t(recipe.width)),
                                        recipe.seed + 1877u) * 2.f - 1.f;
        const auto colorA = rotateHue(recipe.layerA.color, hueNoise * recipe.layerA.hueVariation * 3.14159265f);
        const auto colorB = rotateHue(recipe.layerB.color, hueNoise * recipe.layerB.hueVariation * 3.14159265f);
        for (int channel = 0; channel < 3; ++channel) {
            const float color    = std::lerp(colorA[channel], colorB[channel], mixB);
            const float colorVar = std::lerp(recipe.layerA.colorVariation, recipe.layerB.colorVariation, mixB);
            bake.albedo[index * 4u + std::size_t(channel)] = byte(color * (1.f + variation * colorVar));
        }
        bake.albedo[index * 4u + 3u] = byte(alpha);
        const float roughness =
            std::lerp(recipe.layerA.roughness, recipe.layerB.roughness, mixB) +
            variation * std::lerp(recipe.layerA.roughnessVariation, recipe.layerB.roughnessVariation, mixB);
        bake.params[index * 4u]      = byte(roughness);
        bake.params[index * 4u + 1u] = byte(std::lerp(recipe.layerA.metallic, recipe.layerB.metallic, mixB));
        bake.params[index * 4u + 2u] = byte(std::lerp(recipe.layerA.emissive, recipe.layerB.emissive, mixB));
        height[index]                = clamp01(a * recipe.layerA.height + bWeight * recipe.layerB.height);
        const auto normalLayer = [](float mask, const ProceduralDecalLayer& layer) {
            const float edge0 = std::min(layer.normalTrim * 0.9f, 0.95f);
            float shaped = smoothStep(edge0, std::min(1.f, edge0 + layer.normalSoftness), clamp01(mask));
            if (layer.normalStyle > 0)
                shaped = std::pow(shaped, 0.65f + 0.1f * float(layer.normalStyle % 5));
            const float thickness = 0.5f + std::log2(1.f + layer.normalThickness) * 0.75f;
            return shaped * layer.height * thickness;
        };
        normalHeight[index] = clamp01(normalLayer(a, recipe.layerA) + normalLayer(bWeight, recipe.layerB));
        bake.params[index * 4u + 3u] = byte(height[index]);
        coverage[index]              = alpha;
    }
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            const auto at = [&](int px, int py) {
                return normalHeight[std::size_t(std::clamp(py, 0, recipe.height - 1)) * std::size_t(recipe.width) +
                                    std::size_t(std::clamp(px, 0, recipe.width - 1))];
            };
            const std::size_t index    = std::size_t(y) * std::size_t(recipe.width) + std::size_t(x);
            const float       layerMix = maskB[index] / std::max(maskA[index] + maskB[index], 1e-6f);
            const float strength  = std::lerp(recipe.layerA.normalStrength, recipe.layerB.normalStrength, layerMix);
            float       nx        = (at(x - 1, y) - at(x + 1, y)) * strength;
            float       ny        = (at(x, y - 1) - at(x, y + 1)) * strength;
            const float invLength = 1.f / std::sqrt(nx * nx + ny * ny + 1.f);
            nx *= invLength;
            ny *= invLength;
            const float nz               = invLength;
            bake.normal[index * 4u]      = byte(nx * 0.5f + 0.5f);
            bake.normal[index * 4u + 1u] = byte(ny * 0.5f + 0.5f);
            bake.normal[index * 4u + 2u] = byte(nz * 0.5f + 0.5f);
            bake.normal[index * 4u + 3u] = byte(coverage[index]);
        }
    }
    return Result<ProceduralDecalBake>::success(std::move(bake));
}

}  // namespace eve::decal
