#include "asset/import/VegetationPreset.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>

namespace eve::asset_import {
namespace {

float clamp01(float value) { return std::clamp(value, 0.f, 1.f); }

std::vector<std::size_t> elementIds(const asset::CanonicalMeshData& mesh, std::size_t count) {
    std::vector<std::size_t> parent(count);
    for (std::size_t i = 0; i < count; ++i) parent[i] = i;
    auto root = [&](std::size_t value) {
        while (parent[value] != value) {
            parent[value] = parent[parent[value]];
            value         = parent[value];
        }
        return value;
    };
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= count || b >= count || c >= count) continue;
        const auto ra = root(a), rb = root(b), rc = root(c);
        parent[rb] = ra;
        parent[rc] = ra;
    }
    std::map<std::size_t, std::size_t> ids;
    std::vector<std::size_t>           result(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto r           = root(i);
        auto [found, inserted] = ids.try_emplace(r, ids.size());
        result[i]              = found->second;
    }
    return result;
}

std::vector<float> four(const asset::CanonicalMeshData& mesh, const char* name, std::size_t count,
                        std::array<float, 4> fallback) {
    const auto found = mesh.attributes.find(name);
    if (found != mesh.attributes.end() && found->second.components == 4 && found->second.values.size() == count * 4)
        return found->second.values;
    std::vector<float> result(count * 4);
    for (std::size_t i = 0; i < count; ++i)
        std::copy(fallback.begin(), fallback.end(), result.begin() + std::ptrdiff_t(i * 4));
    return result;
}

Result<int> integer(std::string_view value) {
    int result     = 0;
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || end != value.data() + value.size())
        return Result<int>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, "mesh rule option is not an integer", {}, {}, "asset.import.vegetation-preset.mesh"));
    return Result<int>::success(result);
}

Result<std::vector<float>> mask(const asset::CanonicalMeshData& mesh, const std::vector<std::string>& rule,
                                const std::map<std::string, VegetationPresetImage>& textures, std::size_t count,
                                float radius, float height, float seed) {
    std::vector<float> result(count, 1.f);
    if (rule.empty() || rule[0] == "NONE") return Result<std::vector<float>>::success(std::move(result));
    if (rule.size() < 2) return Result<std::vector<float>>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, "mesh mask rule lacks option", {}, {}, "asset.import.vegetation-preset.mesh"));
    auto option = integer(rule[1]);
    if (!option) return Result<std::vector<float>>::failure(option.status());
    if (rule[0] == "GET_MASK_FROM_CHANNEL") {
        auto      color   = four(mesh, "COLOR_0", count, {1, 1, 1, 1});
        const int channel = option.value();
        for (std::size_t i = 0; i < count; ++i) {
            if (channel < 4)
                result[i] = color[i * 4 + channel];
            else {
                const int uv = (channel - 4) / 4, component = (channel - 4) % 4;
                auto      values = four(mesh, ("_UNITY_UV" + std::to_string(uv)).c_str(), count, {0, 0, 0, 0});
                result[i]        = values[i * 4 + component];
            }
        }
    } else if (rule[0] == "GET_MASK_PROCEDURAL") {
        const int  mode     = option.value();
        const auto elements = (mode == 2 || mode == 3) ? elementIds(mesh, count) : std::vector<std::size_t>{};
        const auto elementCount =
            elements.empty() ? std::size_t(0) : *std::max_element(elements.begin(), elements.end()) + 1;
        for (std::size_t i = 0; i < count; ++i) {
            const float x = mesh.positions[i * 3], y = mesh.positions[i * 3 + 1], z = mesh.positions[i * 3 + 2];
            const float h = height > 0 ? y / height : 0.f, radial = radius > 0 ? std::hypot(x, z) / radius : 0.f;
            switch (mode) {
                case 0: result[i] = 0; break;
                case 1: result[i] = 1; break;
                case 2: {
                    std::uint32_t hash = std::uint32_t(elements[i]) ^ std::bit_cast<std::uint32_t>(seed);
                    hash ^= hash >> 16;
                    hash *= 0x7feb352du;
                    hash ^= hash >> 15;
                    hash *= 0x846ca68bu;
                    hash ^= hash >> 16;
                    result[i] = float(hash & 0x00ffffffu) / float(0x01000000u);
                    break;
                }
                case 3:
                    result[i] = std::fmod(float(elements[i]) / std::max<std::size_t>(elementCount, 1) * seed, 1.f);
                    break;
                case 4: result[i] = clamp01(h); break;
                case 5: result[i] = clamp01(std::sqrt(x * x + y * y + z * z) / std::max(radius, 1e-6f)); break;
                case 6: result[i] = clamp01((radial - .1f) / .9f); break;
                case 7: {
                    const float cap = clamp01((h - .8f) / .2f), base = clamp01(h / .1f);
                    result[i] = clamp01(clamp01((radial - .1f) / .9f) + cap) * base;
                    break;
                }
                case 8: result[i] = 1.f - clamp01(h); break;
                case 9: result[i] = clamp01(-mesh.normals[i * 3 + 1] * .5f + .5f); break;
                case 10: result[i] = clamp01(mesh.normals[i * 3 + 1] * .5f + .5f); break;
                case 11: result[i] = clamp01((clamp01(h) - .2f) / .8f); break;
                case 12: result[i] = clamp01((clamp01(h) - .4f) / .6f); break;
                case 13: result[i] = clamp01((clamp01(h) - .6f) / .4f); break;
                case 14: result[i] = 1.f - std::pow(1.f - clamp01(h), 4.f); break;
                case 15:
                    result[i] = std::pow(clamp01(std::sqrt(x * x + y * y + z * z) / std::max(radius, 1e-6f)), 2.f);
                    break;
                case 16: result[i] = std::pow(clamp01((radial - .1f) / .9f), 2.f); break;
                case 17: {
                    const float cap = clamp01((h - .8f) / .2f), base = clamp01(h / .1f);
                    const float capsule = clamp01(clamp01((radial - .1f) / .9f) + cap) * base;
                    result[i]           = capsule * capsule;
                    break;
                }
                case 18: result[i] = x / std::max(radius, 1e-6f); break;
                case 19: result[i] = h; break;
                case 20: result[i] = z / std::max(radius, 1e-6f); break;
                default:
                    return Result<std::vector<float>>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "unsupported procedural mesh mask mode", {}, {}, "asset.import.vegetation-preset.mesh"));
            }
        }
    } else if (rule[0] == "GET_MASK_FROM_TEXTURE") {
        if (rule.size() < 3)
            return Result<std::vector<float>>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, "texture mask lacks property", {}, {}, "asset.import.vegetation-preset.mesh"));
        const auto image = textures.find(rule[2]);
        if (image == textures.end()) return Result<std::vector<float>>::success(std::move(result));
        const auto pixels = std::uint64_t(image->second.width) * image->second.height;
        if (!image->second.width || !image->second.height || pixels > 16ull * 1024ull * 1024ull ||
            image->second.pixels.size() != std::size_t(pixels) * 4 || option.value() < 0 || option.value() > 3)
            return Result<std::vector<float>>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid texture mask input", {}, {}, "asset.import.vegetation-preset.mesh"));
        int coord = 0;
        for (std::size_t i = 3; i + 1 < rule.size(); ++i)
            if (rule[i] == "GET_COORD") {
                auto parsed = integer(rule[i + 1]);
                if (!parsed) return Result<std::vector<float>>::failure(parsed.status());
                coord = parsed.value();
            }
        auto       uv    = four(mesh, ("_UNITY_UV" + std::to_string(coord)).c_str(), count, {0, 0, 0, 0});
        const auto texel = [&](std::uint32_t x, std::uint32_t y) {
            return image->second.pixels[(std::size_t(y) * image->second.width + x) * 4 + option.value()] / 255.f;
        };
        for (std::size_t i = 0; i < count; ++i) {
            const float px = std::clamp(uv[i * 4] * image->second.width - .5f, 0.f, float(image->second.width - 1));
            const float py =
                std::clamp(uv[i * 4 + 1] * image->second.height - .5f, 0.f, float(image->second.height - 1));
            const auto x0 = std::uint32_t(px), y0 = std::uint32_t(py), x1 = std::min(x0 + 1, image->second.width - 1),
                       y1 = std::min(y0 + 1, image->second.height - 1);
            result[i]     = std::lerp(std::lerp(texel(x0, y0), texel(x1, y0), px - x0),
                                      std::lerp(texel(x0, y1), texel(x1, y1), px - x0), py - y0);
        }
    } else if (rule[0] == "GET_MASK_3RD_PARTY") {
        const auto uv2 = four(mesh, "_UNITY_UV2", count, {0, 0, 0, 0});
        const auto uv1 = four(mesh, "_UNITY_UV1", count, {0, 0, 0, 0});
        const auto uv3 = four(mesh, "_UNITY_UV3", count, {0, 0, 0, 0});
        for (std::size_t i = 0; i < count; ++i) {
            const float x = mesh.positions[i * 3], y = mesh.positions[i * 3 + 1], z = mesh.positions[i * 3 + 2];
            if (option.value() == 0 || option.value() == 1) {
                const float packed = uv2[i * 4], scale = uv2[i * 4 + 1];
                const float pivotX = std::fmod(packed, 1.f) * 2.f - 1.f;
                const float pivotZ = std::fmod(32768.f * packed, 1.f) * 2.f - 1.f;
                const float pivotY = std::sqrt(1.f - clamp01(pivotX * pivotX + pivotZ * pivotZ));
                if (option.value() == 0) {
                    result[i] = std::sqrt(std::pow(x - pivotX * scale, 2.f) + std::pow(y - pivotY * scale, 2.f) +
                                          std::pow(z - pivotZ * scale, 2.f)) /
                                std::max(radius, 1e-6f);
                } else {
                    auto repeat = [](float value) { return value - std::floor(value); };
                    result[i]   = packed < .01f ? 0.f
                                                : repeat(pivotX * scale * 33.3f) + repeat(pivotY * scale * 33.3f) +
                                                      repeat(pivotZ * scale * 33.3f);
                }
            } else if (option.value() == 2) {
                const float ax = uv1[i * 4 + 2] - x, ay = uv1[i * 4 + 3] - y, az = uv2[i * 4 + 3] - z;
                result[i] = uv3[i * 4 + 3] == 0.f ? clamp01(y / std::max(height, 1e-6f))
                                                  : std::sqrt(ax * ax + ay * ay + az * az) *
                                                        (uv1[i * 4 + 3] * uv3[i * 4 + 3]) / std::max(radius, 1e-6f);
            } else {
                return Result<std::vector<float>>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "unsupported third-party vegetation mask mode", {}, {}, "asset.import.vegetation-preset.mesh"));
            }
        }
    } else {
        return Result<std::vector<float>>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "mesh mask source requires texture or vendor adapter", {}, {}, "asset.import.vegetation-preset.mesh"));
    }
    if (rule.size() >= 3 && rule.back().starts_with("ACTION_")) {
        const auto& action = rule.back();
        if (action == "ACTION_ONE_MINUS")
            for (auto& v : result) v = 1.f - v;
        else if (action == "ACTION_NEGATIVE")
            for (auto& v : result) v = -v;
        else if (action == "ACTION_POWER_2")
            for (auto& v : result) v *= v;
        else if (action == "ACTION_MULTIPLY_BY_HEIGHT")
            for (std::size_t i = 0; i < count; ++i)
                result[i] *= clamp01(mesh.positions[i * 3 + 1] / std::max(height, 1e-6f));
        else if (action == "ACTION_CLAMP_NEGATIVE_VALUES") {
            for (std::size_t i = 0; i < count; ++i)
                if (mesh.positions[i * 3 + 1] < 0) result[i] = 0;
        } else if (action == "ACTION_FRACTIONAL_VALUES") {
            for (auto& v : result) v -= std::floor(v);
        } else if (action == "ACTION_REMAP_01") {
            auto [lo, hi]     = std::minmax_element(result.begin(), result.end());
            const float range = *hi - *lo;
            for (auto& v : result) v = range == 0 ? 0 : (v - *lo) / range;
        } else
            return Result<std::vector<float>>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "unsupported mesh mask action", {}, {}, "asset.import.vegetation-preset.mesh"));
    }
    return Result<std::vector<float>>::success(std::move(result));
}

float packPair(float x, float y) { return std::floor(clamp01(x) * 2047.f) * 2048.f + std::floor(clamp01(y) * 2047.f); }

void normalize3(float& x, float& y, float& z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length > 1e-8f) {
        x /= length;
        y /= length;
        z /= length;
    }
}

Result<void> applyNormals(asset::CanonicalMeshData& mesh, const std::vector<std::string>& rule, float height) {
    if (rule.size() != 2 || rule[0] != "GET_NORMALS_PROCEDURAL")
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "unsupported normal rule", {}, {}, "asset.import.vegetation-preset.mesh"));
    auto mode = integer(rule[1]);
    if (!mode || mode.value() < 0 || mode.value() > 6)
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, "invalid normal mode", {}, {}, "asset.import.vegetation-preset.mesh"));
    const std::size_t count = mesh.positions.size() / 3;
    if (mode.value() == 0) {
        std::fill(mesh.normals.begin(), mesh.normals.end(), 0.f);
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
            if (a >= count || b >= count || c >= count)
                return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "mesh index out of range", {}, {}, "asset.import.vegetation-preset.mesh"));
            const float ax = mesh.positions[b * 3] - mesh.positions[a * 3],
                        ay = mesh.positions[b * 3 + 1] - mesh.positions[a * 3 + 1],
                        az = mesh.positions[b * 3 + 2] - mesh.positions[a * 3 + 2];
            const float bx = mesh.positions[c * 3] - mesh.positions[a * 3],
                        by = mesh.positions[c * 3 + 1] - mesh.positions[a * 3 + 1],
                        bz = mesh.positions[c * 3 + 2] - mesh.positions[a * 3 + 2];
            const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
            for (auto v : {a, b, c}) {
                mesh.normals[v * 3] += nx;
                mesh.normals[v * 3 + 1] += ny;
                mesh.normals[v * 3 + 2] += nz;
            }
        }
        for (std::size_t i = 0; i < count; ++i)
            normalize3(mesh.normals[i * 3], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]);
        return Result<void>::success();
    }
    for (std::size_t i = 0; i < count; ++i) {
        float tx = 0, ty = 1, tz = 0, blend = 1;
        if (mode.value() <= 3) {
            blend = mode.value() == 1   ? clamp01(mesh.positions[i * 3 + 1] / std::max(height, 1e-6f))
                    : mode.value() == 2 ? clamp01(clamp01(mesh.positions[i * 3 + 1] / std::max(height, 1e-6f)) + .5f)
                                        : 1.f;
        } else {
            tx = mesh.positions[i * 3];
            ty = mesh.positions[i * 3 + 1];
            tz = mesh.positions[i * 3 + 2];
            normalize3(tx, ty, tz);
            blend = mode.value() == 4 ? .5f : mode.value() == 5 ? .75f : 1.f;
        }
        mesh.normals[i * 3]     = std::lerp(mesh.normals[i * 3], tx, blend);
        mesh.normals[i * 3 + 1] = std::lerp(mesh.normals[i * 3 + 1], ty, blend);
        mesh.normals[i * 3 + 2] = std::lerp(mesh.normals[i * 3 + 2], tz, blend);
    }
    return Result<void>::success();
}

Result<void> recalculateTangents(asset::CanonicalMeshData& mesh) {
    const auto count = mesh.positions.size() / 3;
    const auto uv    = mesh.texcoords.find(0);
    if (uv == mesh.texcoords.end()) {
        mesh.attributes.erase("TANGENT");
        return Result<void>::success();
    }
    if (uv->second.size() != count * 2)
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "canonical UV0 has an invalid tangent input size", {}, {}, "asset.import.vegetation-preset.mesh"));
    std::vector<float> tan1(count * 3), tan2(count * 3);
    for (std::size_t k = 0; k + 2 < mesh.indices.size(); k += 3) {
        const auto a = mesh.indices[k], b = mesh.indices[k + 1], c = mesh.indices[k + 2];
        if (a >= count || b >= count || c >= count)
            return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "mesh index out of range while rebuilding tangents", {}, {}, "asset.import.vegetation-preset.mesh"));
        const float x1 = mesh.positions[b * 3] - mesh.positions[a * 3],
                    x2 = mesh.positions[c * 3] - mesh.positions[a * 3],
                    y1 = mesh.positions[b * 3 + 1] - mesh.positions[a * 3 + 1],
                    y2 = mesh.positions[c * 3 + 1] - mesh.positions[a * 3 + 1],
                    z1 = mesh.positions[b * 3 + 2] - mesh.positions[a * 3 + 2],
                    z2 = mesh.positions[c * 3 + 2] - mesh.positions[a * 3 + 2],
                    s1 = uv->second[b * 2] - uv->second[a * 2], s2 = uv->second[c * 2] - uv->second[a * 2],
                    t1 = uv->second[b * 2 + 1] - uv->second[a * 2 + 1],
                    t2 = uv->second[c * 2 + 1] - uv->second[a * 2 + 1], det = s1 * t2 - s2 * t1;
        if (std::abs(det) < 1e-12f) continue;
        const float r = 1.f / det, sx = (x1 * t2 - x2 * t1) * r, sy = (y1 * t2 - y2 * t1) * r,
                    sz = (z1 * t2 - z2 * t1) * r, tx = (x2 * s1 - x1 * s2) * r, ty = (y2 * s1 - y1 * s2) * r,
                    tz = (z2 * s1 - z1 * s2) * r;
        for (const auto vertex : {a, b, c}) {
            tan1[vertex * 3] += sx;
            tan1[vertex * 3 + 1] += sy;
            tan1[vertex * 3 + 2] += sz;
            tan2[vertex * 3] += tx;
            tan2[vertex * 3 + 1] += ty;
            tan2[vertex * 3 + 2] += tz;
        }
    }
    asset::CanonicalMeshAttribute tangents{4, std::vector<float>(count * 4)};
    for (std::size_t i = 0; i < count; ++i) {
        const float nx = mesh.normals[i * 3], ny = mesh.normals[i * 3 + 1], nz = mesh.normals[i * 3 + 2];
        float       tx = tan1[i * 3], ty = tan1[i * 3 + 1], tz = tan1[i * 3 + 2];
        const float projection = nx * tx + ny * ty + nz * tz;
        tx -= nx * projection;
        ty -= ny * projection;
        tz -= nz * projection;
        if (std::sqrt(tx * tx + ty * ty + tz * tz) <= 1e-8f) {
            if (std::abs(ny) < .999f) {
                tx = nz;
                ty = 0;
                tz = -nx;
            } else {
                tx = 1;
                ty = 0;
                tz = 0;
            }
        }
        normalize3(tx, ty, tz);
        const float bx = ny * tz - nz * ty, by = nz * tx - nx * tz, bz = nx * ty - ny * tx;
        tangents.values[i * 4]     = tx;
        tangents.values[i * 4 + 1] = ty;
        tangents.values[i * 4 + 2] = tz;
        tangents.values[i * 4 + 3] = bx * tan2[i * 3] + by * tan2[i * 3 + 1] + bz * tan2[i * 3 + 2] < 0 ? -1.f : 1.f;
    }
    mesh.attributes["TANGENT"] = std::move(tangents);
    return Result<void>::success();
}
}  // namespace

Result<asset::CanonicalMeshData> executeVegetationMeshRules(
    const VegetationConversionCandidate& candidate, asset::CanonicalMeshData source,
    const std::map<std::string, VegetationPresetImage>& textures, float variationSeed) {
    const std::size_t count = source.positions.size() / 3;
    if (!count || source.positions.size() != count * 3 || source.normals.size() != count * 3 ||
        !std::isfinite(variationSeed))
        return Result<asset::CanonicalMeshData>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid mesh geometry or seed", {}, {}, "asset.import.vegetation-preset.mesh"));
    try {
        float radius = 0, height = 0;
        for (std::size_t i = 0; i < count; ++i) {
            radius =
                std::max(radius, std::max(std::abs(source.positions[i * 3]), std::abs(source.positions[i * 3 + 2])));
            height = std::max(height, std::abs(source.positions[i * 3 + 1]));
        }
        auto color = four(source, "COLOR_0", count, {1, 1, 1, 1});
        auto uv0   = four(source, "_UNITY_UV0", count, {0, 0, 0, 0});
        auto uv1   = four(source, "_UNITY_UV1", count, {0, 0, 0, 0});
        auto apply = [&](const char* ruleName, unsigned component) -> Result<void> {
            auto found = candidate.meshRules.find(ruleName);
            if (found == candidate.meshRules.end()) return Result<void>::success();
            auto values = mask(source, found->second, textures, count, radius, height, variationSeed);
            if (!values) return Result<void>::failure(values.status());
            for (std::size_t i = 0; i < count; ++i) color[i * 4 + component] = values.value()[i];
            return Result<void>::success();
        };
        for (auto [name, component] : {std::pair<const char*, unsigned>{"SetVariation", 0u},
                                       {"SetOcclusion", 1u},
                                       {"SetDetailMask", 2u},
                                       {"SetHeight", 3u}}) {
            auto result = apply(name, component);
            if (!result) return Result<asset::CanonicalMeshData>::failure(result.status());
        }
        auto motion2 =
            candidate.meshRules.contains("SetMotion2")
                ? mask(source, candidate.meshRules.at("SetMotion2"), textures, count, radius, height, variationSeed)
                : Result<std::vector<float>>::success(std::vector<float>(count, 1));
        auto motion3 =
            candidate.meshRules.contains("SetMotion3")
                ? mask(source, candidate.meshRules.at("SetMotion3"), textures, count, radius, height, variationSeed)
                : Result<std::vector<float>>::success(std::vector<float>(count, 1));
        if (!motion2) return Result<asset::CanonicalMeshData>::failure(motion2.status());
        if (!motion3) return Result<asset::CanonicalMeshData>::failure(motion3.status());
        const float packedBounds = packPair(height / 100.f, radius / 100.f);
        for (std::size_t i = 0; i < count; ++i) {
            uv0[i * 4 + 2] = packPair(motion2.value()[i], motion3.value()[i]);
            uv0[i * 4 + 3] = packedBounds;
        }
        if (auto found = candidate.meshRules.find("SetDetailCoord"); found != candidate.meshRules.end() &&
                                                                     found->second.size() >= 2 &&
                                                                     found->second[0] == "GET_COORD_FROM_CHANNEL") {
            auto option = integer(found->second[1]);
            if (!option) return Result<asset::CanonicalMeshData>::failure(option.status());
            auto coord = four(source, ("_UNITY_UV" + std::to_string(option.value())).c_str(), count, {0, 0, 0, 0});
            for (std::size_t i = 0; i < count; ++i) {
                uv1[i * 4 + 2] = coord[i * 4];
                uv1[i * 4 + 3] = coord[i * 4 + 1];
            }
        }
        source.attributes["COLOR_0"]    = {4, std::move(color)};
        source.attributes["_UNITY_UV0"] = {4, std::move(uv0)};
        source.attributes["_UNITY_UV1"] = {4, std::move(uv1)};
        auto uv3                        = four(source, "_UNITY_UV3", count, {0, 0, 0, 0});
        if (auto found = candidate.meshRules.find("SetPivots"); found != candidate.meshRules.end()) {
            if (found->second.size() == 1 && found->second[0] == "NONE") {
                source.attributes["_UNITY_UV3"] = {4, std::move(uv3)};
            } else {
                if (found->second.size() != 2 || found->second[0] != "GET_PIVOTS_PROCEDURAL" || found->second[1] != "0")
                    return Result<asset::CanonicalMeshData>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "unsupported pivot rule", {}, {}, "asset.import.vegetation-preset.mesh"));
                const auto               ids    = elementIds(source, count);
                const auto               groups = *std::max_element(ids.begin(), ids.end()) + 1;
                std::vector<double>      sumX(groups), sumZ(groups);
                std::vector<std::size_t> sizes(groups);
                for (std::size_t i = 0; i < count; ++i) {
                    sumX[ids[i]] += source.positions[i * 3];
                    sumZ[ids[i]] += source.positions[i * 3 + 2];
                    ++sizes[ids[i]];
                }
                for (std::size_t i = 0; i < count; ++i) {
                    uv3[i * 4]     = float(sumX[ids[i]] / sizes[ids[i]]);
                    uv3[i * 4 + 1] = -float(sumZ[ids[i]] / sizes[ids[i]]);
                    uv3[i * 4 + 2] = 0;
                    uv3[i * 4 + 3] = 0;
                }
                source.attributes["_UNITY_UV3"] = {4, std::move(uv3)};
            }
        } else {
            source.attributes["_UNITY_UV3"] = {4, std::move(uv3)};
        }
        if (auto found = candidate.meshRules.find("SetNormals"); found != candidate.meshRules.end()) {
            auto changed = applyNormals(source, found->second, height);
            if (!changed) return Result<asset::CanonicalMeshData>::failure(changed.status());
            changed = recalculateTangents(source);
            if (!changed) return Result<asset::CanonicalMeshData>::failure(changed.status());
        }
        return Result<asset::CanonicalMeshData>::success(std::move(source));
    } catch (const std::bad_alloc&) {
        return Result<asset::CanonicalMeshData>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "mesh conversion allocation failed", {}, {}, "asset.import.vegetation-preset.mesh"));
    }
}

Result<asset::CanonicalMeshData> executeVegetationMeshRules(const VegetationConversionCandidate& candidate,
                                                            asset::CanonicalMeshData source, float variationSeed) {
    static const std::map<std::string, VegetationPresetImage> noTextures;
    return executeVegetationMeshRules(candidate, std::move(source), noTextures, variationSeed);
}
}  // namespace eve::asset_import
