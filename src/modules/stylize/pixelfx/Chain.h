// eve-hot-path
// Native adaptation of pixelfx core/chain.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include <limits>
#include <stdexcept>
#include "stylize/pixelfx/Runtime.h"

namespace eve::stylize::pixelfx {
/** @brief Compiled cue values; position callbacks are trusted static functions, never script closures. */
struct ChainCue {
    PixelFxKind kind;
    double      start = 0, until = std::numeric_limits<double>::infinity(), x = 0, y = 0;
    double (*positionX)(double)       = nullptr;
    void (*options)(PixelFxSettings&) = nullptr;
    bool flip                         = false;
};
struct PreparedCue {
    ChainCue                       cue;
    std::shared_ptr<const Program> program;
};
struct ChainKernel final : Kernel {
    struct Child {
        PreparedCue                    prepared;
        std::unique_ptr<PlaybackState> playback;
        double                         time    = 0;
        bool                           started = false, over = false;
    };
    std::vector<Child> children;
    double             time = 0;
    explicit ChainKernel(const std::vector<PreparedCue>& cues) {
        children.reserve(cues.size());
        for (const auto& c : cues) children.push_back({c, makePlayback(*c.program)});
    }
    void update(double dt) override {
        time += dt;
        for (auto& c : children) {
            if (c.over || time < c.prepared.cue.start) continue;
            // Upstream creates a child on its first eligible tick without advancing it.
            if (!c.started)
                c.started = true;
            else {
                c.time += dt;
                if (c.playback->kernel) c.playback->kernel->update(dt);
            }
            const auto& info = c.prepared.program->descriptor.info;
            if (time >= c.prepared.cue.until || (!info.loop && c.time >= info.duration)) c.over = true;
        }
    }
    void each(Paint& parent, bool back) const {
        for (const auto& c : children) {
            if (!c.started || c.over) continue;
            const auto& cue     = c.prepared.cue;
            const auto& program = *c.prepared.program;
            const auto& d       = program.descriptor;
            Paint       p       = parent;
            p.place(parent.tx + parent.fx * (cue.positionX ? cue.positionX(time) : cue.x), parent.ty + cue.y,
                    (parent.fx < 0) != cue.flip);
            // createFx places each child independently; parent rotation/fade/light are not inherited.
            p.fade         = 1;
            p.gain         = program.settings.lightGain;
            const double t = d.info.loop ? std::fmod(c.time, d.info.duration) : std::min(c.time, d.info.duration);
            if (c.playback->kernel) {
                if (back)
                    c.playback->kernel->drawBack(p);
                else
                    c.playback->kernel->draw(p);
            } else if (program.analytic) {
                if (back)
                    program.analytic->drawBack(p, t, program.settings);
                else
                    program.analytic->draw(p, t, program.settings);
            } else if (back) {
                if (d.drawBack) d.drawBack(p, t, program.settings);
            } else if (d.draw)
                d.draw(p, t, program.settings);
        }
    }
    void drawBack(Paint& p) const override { each(p, true); }
    void draw(Paint& p) const override { each(p, false); }
};
struct ChainRecipe final : SimulationRecipe {
    std::vector<PreparedCue> cues;
    ChainRecipe(std::span<const ChainCue> source, uint32_t seed) {
        cues.reserve(source.size());
        for (size_t i = 0; i < source.size(); ++i) {
            const auto&     cue = source[i];
            const auto&     d   = descriptorFor(cue.kind);
            PixelFxSettings settings;
            d.defaults(settings);
            settings.seed = seed + uint32_t(i);
            if (cue.options) cue.options(settings);
            auto normalized = normalizeOptions(d, settings);
            if (!normalized) throw std::logic_error(normalized.status().describe());
            cues.push_back({cue, makeProgram(d, std::move(normalized).value())});
        }
    }
    std::unique_ptr<Kernel> create(const PixelFxSettings&) const override {
        return std::make_unique<ChainKernel>(cues);
    }
};
}  // namespace eve::stylize::pixelfx
