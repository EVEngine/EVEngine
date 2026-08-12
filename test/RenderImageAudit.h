#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve {
namespace image {
class ImageData;
}
}  // namespace eve

/**
 * Heuristic framebuffer audit for render-pipeline tests.
 *
 * Detects incomplete geometry (interior holes / missing tiles), damage
 * (debug magenta/cyan, torn scanlines), and flicker (static-camera MAD).
 * Findings are recorded with pixel bounding boxes and the test config.
 */
struct RenderAuditConfig {
    std::string scene;
    std::string phase;
    std::string extra;
    int frame = -1;
    int width = 0;
    int height = 0;
};

struct RenderDefect {
    enum class Kind { Incomplete, Damage, Flicker };
    Kind kind = Kind::Incomplete;
    int x = 0, y = 0, w = 0, h = 0;
    float score = 0.f;
    std::string detail;

    static const char *kindName(Kind k) {
        switch (k) {
        case Kind::Incomplete:
            return "incomplete";
        case Kind::Damage:
            return "damage";
        case Kind::Flicker:
            return "flicker";
        }
        return "unknown";
    }
};

struct RenderAuditResult {
    RenderAuditConfig cfg;
    std::vector<RenderDefect> defects;
    float meanLuma = 0.f;
    float occupancy = 0.f;
    float interiorOccupancy = 0.f;
    float flickerMad = 0.f;
    bool empty = false;

    bool hasDefects() const { return empty || !defects.empty(); }
    /** Magenta/tearing/empty/large holes/strong flicker — fail GPU tests. */
    bool hasSevere() const;
};

struct RenderAuditBg {
    float r = 0.08f;
    float g = 0.09f;
    float b = 0.11f;
};

/** @p step > 1 subsamples the framebuffer (flythrough uses 4; keep 1 for synthetic). */
RenderAuditResult auditRgba8(const uint8_t *rgba, int w, int h, const RenderAuditConfig &cfg,
                             const RenderAuditBg &bg = {}, int step = 1);

RenderAuditResult auditImage(const eve::image::ImageData &img, const RenderAuditConfig &cfg,
                             const RenderAuditBg &bg = {}, int step = 1);

RenderAuditResult auditFlickerRgba8(const uint8_t *a, const uint8_t *b, int w, int h,
                                    const RenderAuditConfig &cfg, const RenderAuditBg &bg = {},
                                    int step = 1);

RenderAuditResult auditFlicker(const eve::image::ImageData &a, const eve::image::ImageData &b,
                               const RenderAuditConfig &cfg, const RenderAuditBg &bg = {},
                               int step = 1);

void mergeAudit(RenderAuditResult &dst, const RenderAuditResult &src);

void paintDefectOverlay(eve::image::ImageData &img, const std::vector<RenderDefect> &defs);

bool saveImagePng(const eve::image::ImageData &img, const std::string &path);

/** Append markdown + JSONL under @p outDir (created if needed). */
void appendAuditReport(const std::string &outDir, const RenderAuditResult &r);

/** Save overlay PNG when defects exist; returns overlay path or empty. */
std::string saveAuditOverlay(eve::image::ImageData &img, const RenderAuditResult &r,
                             const std::string &outDir);
