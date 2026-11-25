// eve-hot-path
// Native adaptation of core/act.js update/pose/shake/stop sequencing, MIT (c) 2026 Patch.
// Held-pose/hitstop convention via Hollow Crown, MIT (c) 2026 rehan-remade; see NOTICE.txt.
#include "stylize/pixelfx/ActRuntime.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include "stylize/pixelfx/ActData.h"
#include "stylize/pixelfx/ActTarget.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
Diagnostic invalid(std::string message, std::string path) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(message), std::move(path));
}
double number(const Value& value) {
    if (value.isDouble()) return value.asDouble();
    if (value.isInt64()) return double(value.asInt());
    throw std::invalid_argument("Compiled Act metadata requires numeric fields");
}
double field(const Value& value, const char* name, double fallback) {
    const auto* child = value.find(name);
    return child ? number(*child) : fallback;
}
struct TargetHit {
    double                       push = 1.2, straw = 8, flash = .05, shiver = 0, y = -26;
    std::optional<ActTargetSkin> skin;
    std::optional<double>        at;
    bool                         enabled = true;
};
ActTargetSkin skin(const Value& value) {
    if (value.isNull()) return ActTargetSkin::None;
    const auto                                name = value.asString();
    constexpr std::array<std::string_view, 9> names{"",     "flash", "char",  "ice",   "shadow",
                                                    "holy", "red",   "shock", "poison"};
    for (size_t i = 0; i < names.size(); ++i)
        if (name == names[i]) return ActTargetSkin(i);
    throw std::invalid_argument("Unknown compiled target skin");
}
void hitTarget(ActTargetState& target, const TargetHit& hit) {
    target.omega += hit.push;
    target.flashTime = std::max(target.flashTime, hit.flash);
    if (hit.skin) target.skin = *hit.skin;
    if (hit.shiver != 0) target.shiver = hit.shiver;
    Rng random(target.randomState);
    // Preserve the stream even if its wrapped state is zero (Rng construction's fallback is initialization-only).
    random.state = target.randomState;
    for (int k = 0; k < hit.straw; ++k) {
        const bool back = random.next() < .3;
        ActStraw   p;
        p.x    = target.x + random.range(-3, 3);
        p.y    = hit.y + random.range(-3, 3);
        p.vx   = back ? random.range(-60, -5) : random.range(10, 130);
        p.vy   = random.range(-120, -5);
        p.life = random.range(1.2, 2.2);
        p.big  = random.next() < .5;
        target.straw.push_back(p);
    }
    target.randomState = random.state;
}
void stepTarget(ActTargetState& target, double dt) {
    target.clock += dt;
    target.omega += (-60 * target.theta - 3.4 * target.omega) * dt;
    target.theta = clamp(target.theta + target.omega * dt, -.4, .42);
    if (target.flashTime > 0) target.flashTime -= dt;
    if (target.shiver > 0) target.shiver -= dt;
    for (auto& p : target.straw) {
        p.age += dt;
        p.vy += 210 * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.y >= 0) {
            p.y = 0;
            p.vx *= .2;
            p.vy = 0;
        }
    }
    std::erase_if(target.straw, [](const ActStraw& p) { return p.age >= p.life; });
}
struct Hit {
    double    time, freeze, shake, amp;
    TargetHit target;
    Value     authored;
};
struct DustCue {
    double                time = 0, x = 0, count = 6;
    std::optional<double> direction;
    uint32_t              color = 0;
};
struct DustSpeck {
    double   x, y, vx, vy, age, life;
    bool     big;
    uint32_t color;
};
struct GhostPose {
    rig::AuthoredPose pose;
    double            age = 0;
};
uint32_t sourceColor(const Value& value) {
    if (value.isInt64()) return uint32_t(value.asInt());
    auto text = value.asString();
    if (!text.empty() && text.front() == '#') text.erase(0, 1);
    if (text.size() == 3) text = {text[0], text[0], text[1], text[1], text[2], text[2]};
    if (text.size() != 6) throw std::invalid_argument("Compiled Act color requires RGB");
    size_t     used  = 0;
    const auto color = std::stoul(text, &used, 16);
    if (used != 6) throw std::invalid_argument("Compiled Act color requires hex RGB");
    return hex(uint32_t(color));
}
struct RestArt {
    PixelFxActor                 dummy, hero;
    std::array<int16_t, 24 * 38> nearest{};
};
const RestArt& restArt() {
    static const RestArt art = [] {
        RestArt result;
        auto    load = [](const char* source) {
            auto actor = decodeActor(Value::object(
                {{"schema", Value("eve.pixelfx.actor")}, {"version", Value(int64_t(1))}, {"source", Value(source)}}));
            if (!actor) throw std::logic_error(actor.status().describe());
            return std::move(actor).takeValue();
        };
        result.dummy = load("dummy");
        result.hero  = load("hero");
        for (int r = 0; r < 38; ++r)
            for (int c = 0; c < 24; ++c) {
                int best = -1, distance = 1000000000;
                for (int y = std::max(0, r - 7); y < std::min(38, r + 8); ++y)
                    for (int x = std::max(0, c - 7); x < std::min(24, c + 8); ++x) {
                        const int d = (x - c) * (x - c) + (y - r) * (y - r);
                        if (result.dummy.mask[size_t(y * 24 + x)] && d < distance) {
                            distance = d;
                            best     = y * 24 + x;
                        }
                    }
                result.nearest[size_t(r * 24 + c)] = int16_t(best);
            }
        return result;
    }();
    return art;
}
uint32_t carryColor(uint32_t value, uint32_t rest, uint32_t own) {
    auto lum = [](uint32_t c) { return (.3 * (c & 255u) + .59 * ((c >> 8) & 255u) + .11 * ((c >> 16) & 255u)) / 255; };
    if (lum(value) > .9 || value >> 24 < 255) return value;
    const double k     = clamp(lum(own) / std::max(.08, lum(rest)), .45, 1.5);
    uint32_t     color = 0xff000000u;
    for (int shift : {0, 8, 16})
        color |= uint32_t(std::min(255., jsRound(double((value >> shift) & 255u) * k))) << shift;
    return color;
}
struct Recipe {
    std::shared_ptr<const Program> effect;
    const TargetArt*               art;
    rig::PoseTrack                 track;
    std::vector<Hit>               hits;
    std::vector<double>            targetXs;
    double                         from, to;
    bool                           hasAct;
    uint64_t                       standMask;
    std::vector<DustCue>           dust;
    uint32_t                       ghostColor;
    bool                           onBody;
    double                         follow;
    const RestArt*                 rest;
    std::vector<ActTimingHit>      timingHits;
    std::vector<ActTimingPose>     timingPoses;
};
Result<std::shared_ptr<const Recipe>> prepareRecipe(PixelFxKind kind, const PixelFxSettings& settings) {
    using R                = Result<std::shared_ptr<const Recipe>>;
    const auto& descriptor = descriptorFor(kind);
    auto        normalized = normalizeOptions(descriptor, settings);
    if (!normalized) return R::failure(normalized.status());
    auto metadata = Value::fromJson(actSourceData(kind));
    if (!metadata) return R::failure(metadata.status());
    const auto& act   = metadata.value();
    const auto  stand = rig::standPose();
    Value       keys;
    if (const auto* authored = act.find("poses"))
        keys = *authored;
    else {
        auto          encoded = rig::encodePose(stand.pose);
        Value::Object fields;
        const auto    names = rig::poseFieldNames();
        for (size_t i = 0; i < names.size(); ++i)
            if (stand.contains(rig::PoseField(i)))
                fields.emplace(std::string(names[i]), *encoded.find("fields")->find(std::string(names[i])));
        keys = Value::array({Value::array({Value(0.0), Value::object(std::move(fields))})});
    }
    auto track = rig::PoseTrack::prepare(
        Value::object({{"schema", Value("eve.pixelfx.pose-track")}, {"version", Value(int64_t(1))}, {"keys", keys}}));
    if (!track) return R::failure(track.status());
    const auto*  rows = keys.getIf<Value::Array>();
    const double from = std::min(0.0, number((*rows)[0].at(0)));
    const double to = descriptor.info.loop ? INFINITY : std::max(descriptor.info.duration, number(rows->back().at(0)));
    std::vector<Hit>           hits;
    std::vector<ActTimingHit>  timingHits;
    std::vector<ActTimingPose> timingPoses;
    if (act.find("poses"))
        for (const auto& row : *rows) {
            auto decoded = rig::decodePose(Value::object(
                {{"schema", Value("eve.pixelfx.pose")}, {"version", Value(int64_t(1))}, {"fields", row.at(1)}}));
            if (!decoded) return R::failure(decoded.status());
            rig::AuthoredPose pose{std::move(decoded).takeValue(), 0};
            const auto        names = rig::poseFieldNames();
            for (size_t i = 0; i < names.size(); ++i)
                if (row.at(1).find(std::string(names[i]))) pose.presence |= uint64_t(1) << i;
            const auto*       values = row.getIf<Value::Array>();
            const std::string how = values->size() > 2 && !(*values)[2].isNull() ? (*values)[2].asString() : "smooth";
            timingPoses.push_back({number(row.at(0)), std::move(pose), how});
        }

    if (const auto* hitRows = act.find("hits")) {
        const auto* source = hitRows->getIf<Value::Array>();
        if (!source || source->size() > 4096)
            return R::failure(invalid("Compiled Act hit list outside bounds", "/act/hits"));
        hits.reserve(source->size());
        for (const auto& h : *source) {
            TargetHit target;
            target.push   = field(h, "push", 1.2);
            target.straw  = field(h, "straw", 8);
            target.flash  = field(h, "flash", .05);
            target.shiver = field(h, "shiver", 0);
            target.y      = field(h, "y", -26);
            if (const auto* value = h.find("skin")) target.skin = skin(*value);
            if (const auto* value = h.find("at")) target.at = number(*value);
            if (const auto* value = h.find("target")) target.enabled = !value->isBool() || value->asBool();
            hits.push_back(
                {field(h, "t", 0), field(h, "stop", 0), field(h, "shake", 0), field(h, "amp", 2), target, h});
            timingHits.push_back({field(h, "t", 0), field(h, "stop", 0), field(h, "shake", 0), field(h, "amp", 0),
                                  target.enabled ? target.push : 0, target.skin, target.at});
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.time < b.time; });
    }
    std::vector<double> targetXs;
    if (const auto* targets = act.find("targets")) {
        for (const auto& target : *targets->getIf<Value::Array>()) targetXs.push_back(field(target, "x", 66));
    } else if (const auto* target = act.find("target"))
        targetXs.push_back(field(*target, "x", 66));
    std::vector<DustCue> dust;
    if (const auto* cues = act.find("dust"))
        for (const auto& d : *cues->getIf<Value::Array>()) {
            DustCue cue;
            cue.time  = field(d, "t", 0);
            cue.x     = field(d, "x", 0);
            cue.count = field(d, "n", 6);
            if (const auto* dir = d.find("dir"); dir && !dir->isNull()) cue.direction = number(*dir);
            if (const auto* color = d.find("c")) cue.color = sourceColor(*color);
            dust.push_back(cue);
        }
    uint32_t ghostColor =
        settings.palette.empty() ? hex(0x8fb4ff) : settings.palette[size_t(std::floor(settings.palette.size() * .7))];
    if (const auto* color = act.find("ghost")) ghostColor = sourceColor(*color);
    const auto* bodyFlag = act.find("onBody");
    const bool  onBody   = bodyFlag && bodyFlag->asBool();
    auto        program  = makeProgram(descriptor, std::move(normalized).takeValue());
    return R::success(std::make_shared<const Recipe>(
        Recipe{std::move(program), &targetArt(), std::move(track).takeValue(), std::move(hits), std::move(targetXs),
               from, to, actSourceData(kind) != "{}", stand.presence, std::move(dust), ghostColor, onBody,
               field(act, "follow", 1), &restArt(), std::move(timingHits), std::move(timingPoses)}));
}
}  // namespace
struct ActRenderSession::State {
    struct Hash {
        size_t operator()(const rig::AuthoredPose& key) const noexcept {
            // Typed subset hash; full key equality still checks every defined field.
            size_t result = std::hash<uint64_t>{}(key.presence);
            for (double v : {key.pose.y, key.pose.lean, key.pose.crouch, key.pose.fx, key.pose.sa})
                result ^= std::hash<double>{}(v) + size_t(0x9e3779b9u) + (result << 6) + (result >> 2);
            return result;
        }
    };

    std::unique_ptr<rig::Rasterizer>                                                rasterizer;
    std::unordered_map<rig::AuthoredPose, std::shared_ptr<const rig::Pixels>, Hash> entries;
    uint64_t                                   hits = 0, evaluations = 0, clears = 0;
    Result<std::shared_ptr<const rig::Pixels>> raster(const rig::AuthoredPose& pose) {
        auto key = rig::rasterCacheKey(pose);
        if (auto found = entries.find(key); found != entries.end()) {
            ++hits;
            return Result<std::shared_ptr<const rig::Pixels>>::success(found->second);
        }
        if (!rasterizer) rasterizer = std::make_unique<rig::Rasterizer>();
        auto result = rasterizer->render(pose.pose);
        if (!result) return Result<std::shared_ptr<const rig::Pixels>>::failure(result.status());
        auto pixels = std::make_shared<const rig::Pixels>(std::move(result).takeValue());
        if (entries.size() > 6000) {
            entries.clear();
            ++clears;
        }
        entries.emplace(std::move(key), pixels);
        ++evaluations;
        return Result<std::shared_ptr<const rig::Pixels>>::success(std::move(pixels));
    }
};
ActRenderSession::ActRenderSession() : state_(std::make_unique<State>()) {}
ActRenderSession::~ActRenderSession() = default;
ActRenderCacheStats ActRenderSession::cacheStats() const noexcept {
    return {state_->entries.size(), state_->hits, state_->evaluations, state_->clears};
}
struct ActRuntime::State {
    std::shared_ptr<const Recipe> recipe;
    std::unique_ptr<Kernel>       kernel;
    double                      wall = 0, time = 0, effectTime = 0, freeze = 0, shakeTime = 0, shakeStart = 0, amp = 0;
    std::optional<double>       stopAt;
    bool                        stopped  = false;
    bool                        updating = false;
    size_t                      nextHit  = 0;
    uint64_t                    loops    = 0;
    rig::AuthoredPose           pose;
    std::vector<ActHitEvent>    events;
    std::vector<ActTargetState> targets;
    std::vector<DustSpeck>      specks;
    std::vector<GhostPose>      ghosts;
    Rng                         dustRandom;
    size_t                      nextDust         = 0;
    double                      ghostAccumulator = 0;
    struct FrameScratch {
        std::vector<uint32_t> backColor, backLight, frontColor, frontLight;
    };
    FrameScratch                               frameScratch;
    std::shared_ptr<ActRenderSession>          renderSession;
    Result<std::shared_ptr<const rig::Pixels>> raster(const rig::AuthoredPose& poseValue) {
        return renderSession->state_->raster(poseValue);
    }
    explicit State(std::shared_ptr<const Recipe> r, std::shared_ptr<ActRenderSession> session)
        : recipe(std::move(r)),
          time(recipe->from),
          dustRandom(recipe->effect->settings.seed + 9u),
          renderSession(std::move(session)) {
        // Reuse the sole mutable-kernel factory; the standalone fixed-tick wrapper is not retained.
        auto prepared = makePlayback(*recipe->effect);
        kernel        = std::move(prepared->kernel);
        events.reserve(recipe->hits.size());
        ghosts.reserve(7);
        targets.reserve(recipe->targetXs.size());
        for (size_t i = 0; i < recipe->targetXs.size(); ++i) {
            ActTargetState target;
            target.x           = recipe->targetXs[i];
            target.randomState = Rng(recipe->effect->settings.seed + 4u + uint32_t(i)).state;
            targets.push_back(std::move(target));
        }
        poseNow();
    }
    void poseNow() {
        const auto&  info  = recipe->effect->descriptor.info;
        const double t     = info.loop && time >= info.duration ? std::fmod(time, info.duration) : time;
        auto         value = recipe->track.sampleAuthored(t);
        if (!value) throw std::logic_error(value.status().describe());
        pose = std::move(value).takeValue();
        if (!info.loop) {
            auto held = recipe->track.isHeld(t);
            if (!held) throw std::logic_error(held.status().describe());
            if (held.value()) {
                const int up = int(int64_t(std::floor(t * 3)) & 1);
                pose.pose.crouch += up;
                pose.pose.fy += up;
                pose.pose.by += up;
                pose.presence |= recipe->standMask;
            }
        }
    }
    void update(double dt, const std::function<void(const Value&, const ActHitEvent&)>& observer) {
        events.clear();
        wall += dt;
        if (shakeTime > 0) shakeTime -= dt;
        if (freeze > 0) {
            freeze -= dt;
            return;
        }
        const double before = time;
        time += dt;
        if (time >= 0) {
            const double delta = before < 0 ? time : dt;
            effectTime += delta;
            if (kernel) {
                const int n = std::max(1, int(std::ceil(delta * 120 - 1e-9)));
                for (int i = 0; i < n; ++i) kernel->update(delta / n);
            }
        }
        const auto& info = recipe->effect->descriptor.info;
        if (info.loop && time >= info.duration) {
            const auto loop = uint64_t(std::floor(time / info.duration));
            if (loop != loops) {
                loops    = loop;
                nextHit  = 0;
                nextDust = 0;
            }
        }
        const double t = info.loop && time >= info.duration ? std::fmod(time, info.duration) : time;
        while (nextHit < recipe->hits.size() && recipe->hits[nextHit].time <= t) {
            const auto& hit = recipe->hits[nextHit];
            if (hit.freeze != 0) freeze = std::max(freeze, hit.freeze);
            if (hit.shake != 0) {
                shakeTime = shakeStart = hit.shake;
                amp                    = hit.amp;
            }
            if (hit.target.enabled)
                for (size_t i = 0; i < targets.size(); ++i)
                    if (!hit.target.at || *hit.target.at == double(i)) hitTarget(targets[i], hit.target);
            events.push_back({nextHit, hit.time, time, effectTime});
            if (observer) observer(hit.authored, events.back());
            ++nextHit;
        }
        while (nextDust < recipe->dust.size() && recipe->dust[nextDust].time <= t) {
            const auto& cue = recipe->dust[nextDust++];
            for (int i = 0; i < cue.count; ++i) {
                const double direction = cue.direction ? *cue.direction : (dustRandom.next() < .5 ? -1 : 1);
                DustSpeck    p;
                p.x     = cue.x + dustRandom.range(-3, 3);
                p.y     = -1 - dustRandom.range(0, 2);
                p.vx    = direction * dustRandom.range(22, 90);
                p.vy    = dustRandom.range(-34, -6);
                p.age   = 0;
                p.life  = dustRandom.range(.16, .4);
                p.big   = dustRandom.next() < .45;
                p.color = cue.color;
                specks.push_back(p);
            }
        }
        for (auto& p : specks) {
            p.age += dt;
            p.vx *= 1 - 5 * dt;
            p.vy += 110 * dt;
            p.x += p.vx * dt;
            p.y = std::min(-1., p.y + p.vy * dt);
        }
        std::erase_if(specks, [](const DustSpeck& p) { return p.age >= p.life; });
        poseNow();
        for (auto& target : targets) stepTarget(target, dt);
        ghostAccumulator += dt;
        if (pose.pose.ghost != 0 && ghostAccumulator >= 1.0 / 30) {
            ghostAccumulator = 0;
            if (ghosts.size() == 7) ghosts.erase(ghosts.begin());
            ghosts.push_back({pose, 0});
        }
        for (auto& ghost : ghosts) ghost.age += dt;
        while (!ghosts.empty() && ghosts.front().age > .16) ghosts.erase(ghosts.begin());
    }
};
ActRuntime::ActRuntime(std::unique_ptr<State> state) : state_(std::move(state)) {}
ActRuntime::~ActRuntime() = default;
Result<ActPreparedRuntime> ActRuntime::prepare(PixelFxKind kind, const PixelFxSettings& settings) {
    return prepareInSession(kind, settings, std::make_shared<ActRenderSession>());
}
Result<ActPreparedRuntime> ActRuntime::prepareInSession(PixelFxKind kind, const PixelFxSettings& settings,
                                                        std::shared_ptr<ActRenderSession> session) {
    using R = Result<ActPreparedRuntime>;
    if (!session) return R::failure(invalid("Render session must be owned", "/act/session"));
    try {
        auto recipe = prepareRecipe(kind, settings);
        if (!recipe) return R::failure(recipe.status());
        auto state = std::make_unique<State>(std::move(recipe).takeValue(), std::move(session));
        return R::success(ActPreparedRuntime(std::unique_ptr<ActRuntime>(new ActRuntime(std::move(state)))));
    } catch (const std::invalid_argument& error) {
        return R::failure(invalid(error.what(), "/act/source"));
    }
}
ActPreparedRuntime::ActPreparedRuntime(std::unique_ptr<ActRuntime> runtime) : runtime_(std::move(runtime)) {}
ActPreparedRuntime::~ActPreparedRuntime()                                                        = default;
ActPreparedRuntime::ActPreparedRuntime(ActPreparedRuntime&&) noexcept                            = default;
ActPreparedRuntime&                 ActPreparedRuntime::operator=(ActPreparedRuntime&&) noexcept = default;
Result<std::unique_ptr<ActRuntime>> ActRuntime::publish(ActPreparedRuntime&& prepared) {
    using R = Result<std::unique_ptr<ActRuntime>>;
    if (!prepared.runtime_) return R::failure(invalid("Act preparation was already consumed", "/act/candidate"));
    return R::success(std::move(prepared.runtime_));
}
Result<void>               ActRuntime::step(double delta) { return step(delta, {}); }
Result<ActPreparedRuntime> ActRuntime::prepareReplay(std::span<const ActReplayCommand> commands) const {
    using R = Result<ActPreparedRuntime>;
    if (state_->updating)
        return R::failure(invalid("Act cannot prepare replay from its hit observer", "/act/reentrancy"));
    constexpr size_t limit = 1000000;
    if (commands.size() > limit) return R::failure(invalid("Replay command budget exceeded", "/act/replay/budget"));
    size_t substeps = 0;
    double wall     = 0;
    for (const auto& command : commands) {
        if (command.operation == ActReplayOperation::Stop) {
            if (command.delta != 0) return R::failure(invalid("Stop has no delta", "/act/replay/delta"));
        } else if (command.operation == ActReplayOperation::Step) {
            const double dt = command.delta;
            if (!std::isfinite(dt) || dt < 0 || dt > 1 || wall + dt > 1e6)
                return R::failure(invalid("Invalid replay update delta", "/act/replay/delta"));
            wall += dt;
            substeps += size_t(std::max(1., std::ceil(dt * 120 - 1e-9)));
            if (substeps > limit) return R::failure(invalid("Replay substep budget exceeded", "/act/replay/budget"));
        } else
            return R::failure(invalid("Unknown replay operation", "/act/replay/operation"));
    }
    auto replacement =
        std::unique_ptr<ActRuntime>(new ActRuntime(std::make_unique<State>(state_->recipe, state_->renderSession)));
    for (const auto& command : commands) {
        if (command.operation == ActReplayOperation::Stop)
            replacement->stop();
        else {
            auto advanced = replacement->step(command.delta);
            if (!advanced) return R::failure(advanced.status());
        }
    }
    return R::success(ActPreparedRuntime(std::move(replacement)));
}
Result<void> ActRuntime::step(double delta, const std::function<void(const Value&, const ActHitEvent&)>& observer) {
    if (state_->updating)
        return Result<void>::failure(invalid("Act cannot step recursively from a hit observer", "/act/reentrancy"));
    if (!std::isfinite(delta) || delta < 0 || delta > 1 || state_->wall + delta > 1e6)
        return Result<void>::failure(
            invalid("Act delta must be finite in0..1s and total wall time at most1e6s", "/act/delta"));
    struct UpdateGuard {
        bool& active;
        explicit UpdateGuard(bool& value) : active(value) { active = true; }
        ~UpdateGuard() { active = false; }
    } guard(state_->updating);
    state_->update(delta, observer);
    return Result<void>::success();
}
void ActRuntime::stop() noexcept {
    if (!state_->stopAt) state_->stopAt = state_->effectTime;
    state_->stopped = true;
}
ActSnapshot ActRuntime::snapshot() const {
    const auto& s      = *state_;
    const auto& info   = s.recipe->effect->descriptor.info;
    const bool  fxDone = (s.stopAt && s.effectTime - *s.stopAt >= .3) || (!info.loop && s.effectTime >= info.duration);
    ActSnapshot out{
        s.wall,           s.time,       s.effectTime, s.freeze,
        s.shakeTime,      s.freeze > 0, s.stopped,    s.stopped ? fxDone : (s.time >= s.recipe->to && s.freeze <= 0),
        s.recipe->hasAct, s.loops,      {},           s.pose};
    if (s.shakeTime > 0) {
        const double k = s.amp * (s.shakeTime / s.shakeStart), f = std::floor(s.shakeTime * 60);
        out.shake = {int(jsRound(std::sin(f * 2.4) * k)), int(jsRound(std::cos(f * 3.7) * k * .7))};
    }
    return out;
}
std::span<const ActHitEvent>    ActRuntime::hitEvents() const noexcept { return state_->events; }
std::span<const ActTargetState> ActRuntime::targetStates() const noexcept { return state_->targets; }
std::optional<ActTimingView>    ActRuntime::timing() const noexcept {
    const auto& recipe = *state_->recipe;
    if (!recipe.hasAct) return std::nullopt;
    const auto& info = recipe.effect->descriptor.info;
    return ActTimingView{-recipe.from,      info.loop ? 0 : std::max(0., recipe.to - info.duration),
                         recipe.onBody,     recipe.targetXs,
                         recipe.timingHits, recipe.timingPoses};
}
Result<ActPreparedBounds> ActRuntime::prepareBounds() {
    using R            = Result<ActPreparedBounds>;
    const auto& recipe = *state_->recipe;
    const auto& info   = recipe.effect->descriptor.info;
    ActBounds   box{-double(info.originX), -double(info.originY), double(info.width - info.originX),
                    double(info.height - info.originY)};
    auto        see = [&](const rig::AuthoredPose& pose) -> Result<void> {
        auto raster = state_->raster(pose);
        if (!raster) return Result<void>::failure(raster.status());
        const auto&  pixels = *raster.value();
        const double x = jsRound(pose.pose.x), y = jsRound(pose.pose.y);
        box.x0 = std::min(box.x0, pixels.x0 + x - 1);
        box.x1 = std::max(box.x1, pixels.x1 + x + 1);
        box.y0 = std::min(box.y0, pixels.y0 + y - 1);
        box.y1 = std::max(box.y1, pixels.y1 + y + 1);
        return Result<void>::success();
    };
    if (recipe.timingPoses.empty()) {
        auto result = see(rig::standPose());
        if (!result) return R::failure(result.status());
    } else {
        for (double t = recipe.timingPoses.front().time; t <= recipe.timingPoses.back().time; t += 1.0 / 12) {
            auto pose = recipe.track.sampleAuthored(t);
            if (!pose) return R::failure(pose.status());
            auto result = see(pose.value());
            if (!result) return R::failure(result.status());
        }
        for (const auto& key : recipe.timingPoses) {
            auto result = see(key.pose);
            if (!result) return R::failure(result.status());
        }
    }
    for (double x : recipe.targetXs) {
        box.x0 = std::min(box.x0, x - 30);
        box.x1 = std::max(box.x1, x + 36);
        box.y0 = std::min(box.y0, -56.);
    }
    return R::success(ActPreparedBounds(box));
}
PixelFxInfo  ActRuntime::info() const { return state_->recipe->effect->descriptor.info; }
Result<void> ActRuntime::drawEffectLayers(PixelFxCanvas canvas, PixelFxLayer layer, PixelFxPlacement placement) const {
    auto validated = validateCanvas(canvas, layer, placement);
    if (!validated) return validated;
    if (state_->time < 0) return Result<void>::success();
    auto info           = this->info();
    info.width          = canvas.width;
    info.height         = canvas.height;
    info.originX        = canvas.originX;
    info.originY        = canvas.originY;
    const auto& program = *state_->recipe->effect;
    Paint       painter{info, canvas.color, canvas.light, program.settings.lightGain};
    painter.place(placement.x, placement.y, placement.flip, placement.angle);
    if (state_->stopAt) painter.fade = clamp(1 - (state_->effectTime - *state_->stopAt) / .3);
    // createAct invokes createFx.draw separately for back/front; each begins with fresh placement/fade.
    if (layer != PixelFxLayer::Front) {
        auto back = painter;
        drawProgramLayer(program, state_->kernel.get(), state_->effectTime, back, Paint::Layer::Back);
    }
    if (layer != PixelFxLayer::Back) {
        auto front = painter;
        drawProgramLayer(program, state_->kernel.get(), state_->effectTime, front, Paint::Layer::Front);
    }
    return Result<void>::success();
}
Result<void> ActRuntime::drawWithoutBody(PixelFxCanvas canvas, PixelFxPlacement placement, bool showTargets) const {
    auto validated = validateCanvas(canvas, PixelFxLayer::Both, placement);
    if (!validated) return validated;
    auto back = drawEffectLayers(canvas, PixelFxLayer::Back, placement);
    if (!back) return back;
    if (showTargets) drawActTargets(*state_->recipe->art, state_->targets, canvas, placement, false);
    auto front = drawEffectLayers(canvas, PixelFxLayer::Front, placement);
    if (!front) return front;
    if (showTargets) drawActTargets(*state_->recipe->art, state_->targets, canvas, placement, true);
    return Result<void>::success();
}
struct ActPreparedFrame::State {
    int                   width, height, originX, originY;
    std::vector<uint32_t> color, light;
    explicit State(PixelFxCanvas input)
        : width(input.width),
          height(input.height),
          originX(input.originX),
          originY(input.originY),
          color(input.color.begin(), input.color.end()),
          light(input.light.begin(), input.light.end()) {}
    PixelFxCanvas canvas() { return {width, height, originX, originY, color, light}; }
};
ActPreparedFrame::ActPreparedFrame(std::unique_ptr<State> state) : state_(std::move(state)) {}
ActPreparedFrame::~ActPreparedFrame()                                      = default;
ActPreparedFrame::ActPreparedFrame(ActPreparedFrame&&) noexcept            = default;
ActPreparedFrame& ActPreparedFrame::operator=(ActPreparedFrame&&) noexcept = default;
Result<void>      ActPreparedFrame::copyTo(PixelFxCanvas canvas) const {
    if (!state_) return Result<void>::failure(invalid("Frame token was moved", "/act/frame"));
    auto validated = validateCanvas(canvas, PixelFxLayer::Both, {});
    if (!validated) return validated;
    if (canvas.width != state_->width || canvas.height != state_->height || canvas.originX != state_->originX ||
        canvas.originY != state_->originY)
        return Result<void>::failure(
            invalid("Frame canvas geometry must match its captured geometry", "/act/frame/canvas"));
    std::copy(state_->color.begin(), state_->color.end(), canvas.color.begin());
    std::copy(state_->light.begin(), state_->light.end(), canvas.light.begin());
    return Result<void>::success();
}
Result<ActPreparedFrame> ActRuntime::prepareFrame(PixelFxCanvas background, PixelFxPlacement placement,
                                                  bool showTargets) {
    return prepareFrame(background, placement, showTargets, ActBodyPresentation::SourceRig);
}
Result<ActPreparedFrame> ActRuntime::prepareFrame(PixelFxCanvas background, PixelFxPlacement placement,
                                                  bool showTargets, ActBodyPresentation presentation) {
    using R = Result<ActPreparedFrame>;
    if (presentation != ActBodyPresentation::SourceRig && presentation != ActBodyPresentation::InputActor &&
        presentation != ActBodyPresentation::EffectsOnly)
        return R::failure(invalid("Unknown Act body presentation", "/act/body"));
    const bool  inputBody  = presentation == ActBodyPresentation::InputActor;
    const auto& inputActor = state_->recipe->effect->settings.actor;
    auto        validated  = validateCanvas(background, PixelFxLayer::Both, placement);
    if (!validated) return R::failure(validated.status());
    if (presentation == ActBodyPresentation::EffectsOnly) {
        auto frame = std::make_unique<ActPreparedFrame::State>(background);
        auto drawn = drawWithoutBody(frame->canvas(), placement, showTargets);
        if (!drawn) return R::failure(drawn.status());
        return R::success(ActPreparedFrame(std::move(frame)));
    }
    const rig::Pixels                               emptyPixels;
    std::shared_ptr<const rig::Pixels>              body;
    std::vector<std::shared_ptr<const rig::Pixels>> ghostPixels;
    if (!inputBody) {
        auto raster = state_->raster(state_->pose);
        if (!raster) return R::failure(raster.status());
        body = std::move(raster).takeValue();
        ghostPixels.reserve(state_->ghosts.size());
        for (const auto& ghost : state_->ghosts) {
            auto pixels = state_->raster(ghost.pose);
            if (!pixels) return R::failure(pixels.status());
            ghostPixels.push_back(std::move(pixels).takeValue());
        }
    }
    auto         frame  = std::make_unique<ActPreparedFrame::State>(background);
    auto         canvas = frame->canvas();
    const auto&  pose   = state_->pose;
    const double X      = (inputBody ? jsRound(placement.x) : placement.x) + canvas.originX,
                 Y = (inputBody ? jsRound(placement.y) : placement.y) + canvas.originY, rx = jsRound(pose.pose.x),
                 ry    = jsRound(pose.pose.y);
    auto         index = [&](double x, double y) -> std::optional<size_t> {
        if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return std::nullopt;
        const double j = y * canvas.width + x;
        if (j != std::floor(j) || j < 0 || j >= double(canvas.color.size())) return std::nullopt;
        return size_t(j);
    };
    const double cx     = X + (placement.flip ? -rx : rx);
    auto         shadow = [&] {
        if (inputBody && inputActor.pixels.empty()) return;
        const int feet = pose.contains(rig::PoseField::Ffx) && pose.contains(rig::PoseField::Fbx)
                             ? int(jsUint(std::abs(pose.pose.ffx - pose.pose.fbx)) >> 1)
                             : 0;
        const int half = 10 + feet - std::min(8, int(jsUint(std::abs(ry)) >> 1));
        if (Y >= 0 && Y < canvas.height)
            for (int dy = 0; dy < 2; ++dy)
                for (int x = -half + dy * 2; x <= half - dy * 2; ++x)
                    if (auto j = index(cx + x, Y + dy))
                        canvas.color[*j] =
                            (canvas.color[*j] & 0xffffffu) | (std::max(canvas.color[*j] >> 24, 110u) << 24);
    };
    auto inputPoint = [&](size_t i, const rig::AuthoredPose& p) {
        const double lx = std::floor(inputActor.x) + double(i % size_t(inputActor.width));
        const double ly = std::floor(inputActor.y) + double(i / size_t(inputActor.width));
        const double k  = clamp((-11 - ly) / 7);
        return std::array<double, 2>{lx + jsRound(p.pose.lean * k + jsRound(p.pose.x)),
                                     ly + jsRound(p.pose.crouch * k + jsRound(p.pose.y))};
    };
    auto drawGhosts = [&] {
        for (size_t g = 0; g < state_->ghosts.size(); ++g) {
            const auto&  ghost  = state_->ghosts[g];
            const auto&  pixels = inputBody ? emptyPixels : *ghostPixels[g];
            const double gx     = X + (placement.flip ? -jsRound(ghost.pose.pose.x) : jsRound(ghost.pose.pose.x)),
                         gy = Y + jsRound(ghost.pose.pose.y), k = .5 * (1 - ghost.age / .16);
            const auto   color = state_->recipe->ghostColor;
            const size_t count = inputBody ? inputActor.pixels.size() : pixels.color.size();
            for (size_t i = 0; i < count; ++i) {
                if (inputBody && !(inputActor.pixels[i] >> 24)) continue;
                const auto   point   = inputBody ? inputPoint(i, ghost.pose) : std::array<double, 2>{};
                const double px      = inputBody ? X + (placement.flip ? -point[0] - 1 : point[0])
                                                 : gx + (placement.flip ? -pixels.xs[i] - 1 : pixels.xs[i]);
                const double py      = inputBody ? Y + point[1] : gy + pixels.ys[i];
                const double opacity = inputBody ? double(inputActor.pixels[i] >> 24) / 255 : 1;
                if (auto j = index(px, py)) {
                    const auto d      = canvas.light[*j];
                    uint32_t   result = 0xff000000u;
                    for (int shift : {0, 8, 16})
                        result |= jsUint(std::min(255., double((d >> shift) & 255u) +
                                                            double((color >> shift) & 255u) * k * opacity))
                                  << shift;
                    canvas.light[*j] = result;
                }
            }
        }
    };
    const auto& pixels = inputBody ? emptyPixels : *body;
    auto        paint  = [&](size_t i, uint32_t color) {
        const auto   point = inputBody ? inputPoint(i, pose) : std::array<double, 2>{};
        const double px    = inputBody ? X + (placement.flip ? -point[0] - 1 : point[0])
                                       : cx + (placement.flip ? -pixels.xs[i] - 1 : pixels.xs[i]);
        const double py    = inputBody ? Y + point[1] : Y + ry + pixels.ys[i];
        if (auto j = index(px, py)) {
            canvas.color[*j] = color;
            if (!state_->ghosts.empty()) canvas.light[*j] = 0;
        }
    };
    if (!state_->recipe->onBody) {
        auto back = drawEffectLayers(canvas, PixelFxLayer::Back, placement);
        if (!back) return R::failure(back.status());
        if (showTargets) drawActTargets(*state_->recipe->art, state_->targets, canvas, placement, false);
        shadow();
        drawGhosts();
        if (inputBody) {
            for (size_t i = 0; i < inputActor.pixels.size(); ++i)
                if (inputActor.pixels[i] >> 24) paint(i, inputActor.pixels[i]);
        } else
            for (size_t i = 0; i < pixels.color.size(); ++i) paint(i, pixels.color[i]);
        auto front = drawEffectLayers(canvas, PixelFxLayer::Front, placement);
        if (!front) return R::failure(front.status());
    } else {
        auto& scratch = state_->frameScratch;
        auto& ac      = scratch.backColor;
        auto& al      = scratch.backLight;
        auto& bc      = scratch.frontColor;
        auto& bl      = scratch.frontLight;
        for (auto* plane : {&ac, &al, &bc, &bl}) plane->resize(canvas.color.size());
        std::fill(ac.begin(), ac.end(), 0);
        std::fill(al.begin(), al.end(), 0);
        PixelFxCanvas A{canvas.width, canvas.height, canvas.originX, canvas.originY, ac, al},
            B{canvas.width, canvas.height, canvas.originX, canvas.originY, bc, bl};
        auto back = drawEffectLayers(A, PixelFxLayer::Back, placement);
        if (!back) return R::failure(back.status());
        std::copy(ac.begin(), ac.end(), bc.begin());
        std::copy(al.begin(), al.end(), bl.begin());
        const auto&  program    = *state_->recipe->effect;
        const auto&  info       = program.descriptor.info;
        const double time       = info.loop ? (state_->time < 0 ? 0 : std::fmod(state_->time, info.duration))
                                            : clamp(state_->time, 0, info.duration);
        const auto   visibility = state_->time < 0   ? PixelFxActorVisibility::Visible
                                  : state_->kernel   ? state_->kernel->actorVisibility(time)
                                  : program.analytic ? program.analytic->actorVisibility(time)
                                                     : PixelFxActorVisibility::Visible;
        const bool   alpha      = visibility == PixelFxActorVisibility::Visible;
        const auto&  rest       = *state_->recipe->rest;
        const bool   custom     = inputActor.source == PixelFxActorSource::Custom;
        const auto&  actor =
            inputBody
                ? inputActor
                : (inputActor.source == PixelFxActorSource::Hero || (custom && inputActor.scale != 2) ? rest.hero
                                                                                                      : rest.dummy);
        const double actorX = custom ? inputActor.x : actor.x, actorY = custom ? inputActor.y : actor.y;
        auto         actorInfo = info;
        actorInfo.width        = B.width;
        actorInfo.height       = B.height;
        actorInfo.originX      = B.originX;
        actorInfo.originY      = B.originY;
        Paint actorPainter{actorInfo, bc, bl};
        actorPainter.place(inputBody ? placement.x : X, inputBody ? placement.y : Y, placement.flip);
        if (alpha)
            for (int y = 0; y < actor.height; ++y)
                for (int x = 0; x < actor.width; ++x) {
                    const auto color = actor.pixels[size_t(y * actor.width + x)];
                    if (color) actorPainter.dot(actorX + x, actorY + y, color);
                }
        auto front = drawEffectLayers(B, PixelFxLayer::Front, placement);
        if (!front) return R::failure(front.status());
        const double lean = pose.pose.lean * state_->recipe->follow, cr = pose.pose.crouch * state_->recipe->follow;
        auto         copy = [&](bool light, int sx, int sy, uint32_t value) {
            const double ly = sy - Y, k = clamp((-11 - ly) / 7), f = clamp((-1 - ly) / 6);
            const double dx = jsRound(lean * k + rx * f), dy = jsRound(cr * k + ry * f);
            if (auto j = index(sx + (placement.flip ? -dx : dx), sy + dy)) {
                if (light) {
                    const auto old    = canvas.light[*j];
                    uint32_t   result = 0xff000000u;
                    for (int shift : {0, 8, 16})
                        result |= std::min(255u, ((old >> shift) & 255u) + ((value >> shift) & 255u)) << shift;
                    canvas.light[*j] = result;
                } else if (value >> 24 == 255 || !canvas.color[*j])
                    canvas.color[*j] = value;
                else {
                    const double alphaValue = double(value >> 24) / 255;
                    const auto   old        = canvas.color[*j];
                    uint32_t     result     = std::max(value >> 24, old >> 24) << 24;
                    for (int shift : {0, 8, 16})
                        result |= uint32_t(jsRound(double((value >> shift) & 255u) * alphaValue +
                                                   double((old >> shift) & 255u) * (1 - alphaValue)))
                                  << shift;
                    canvas.color[*j] = result;
                }
            }
        };
        if (showTargets) drawActTargets(*state_->recipe->art, state_->targets, canvas, placement, false);
        shadow();
        for (int sy = 0; sy < canvas.height; ++sy)
            for (int sx = 0; sx < canvas.width; ++sx) {
                const auto color = ac[size_t(sy * canvas.width + sx)];
                if (color) copy(false, sx, sy, color);
            }
        drawGhosts();
        if (inputBody) {
            for (size_t i = 0; i < inputActor.pixels.size(); ++i) {
                if (!(inputActor.pixels[i] >> 24)) continue;
                const double   lx     = std::floor(inputActor.x) + double(i % size_t(inputActor.width));
                const double   ly     = std::floor(inputActor.y) + double(i / size_t(inputActor.width));
                const auto     source = index(X + (placement.flip ? -lx - 1 : lx), Y + ly);
                const uint32_t value  = source ? bc[*source] : (alpha ? inputActor.pixels[i] : 0);
                if (value) paint(i, value);
            }
        } else
            for (size_t i = 0; i < pixels.color.size(); ++i) {
                const int mapped = pixels.src[i] >= 0 ? rest.nearest[size_t(pixels.src[i])] : -1;
                if (mapped < 0) {
                    if (alpha) paint(i, pixels.color[i]);
                    continue;
                }
                const int    c = mapped % 24, r = mapped / 24;
                const double sx = placement.flip ? X - (c - 12) - 1 : X + c - 12, sy = Y + r - 38;
                uint32_t     value = rest.dummy.pixels[size_t(mapped)];
                if (sx >= 0 && sy >= 0 && sx < canvas.width && sy < canvas.height) {
                    auto j = index(sx, sy);
                    value  = j ? bc[*j] : 0;
                }
                if (!value) continue;
                paint(i, value == rest.dummy.pixels[size_t(mapped)]
                             ? pixels.color[i]
                             : carryColor(value, rest.dummy.pixels[size_t(mapped)], pixels.color[i]));
            }
        for (int sy = 0; sy < canvas.height; ++sy)
            for (int sx = 0; sx < canvas.width; ++sx) {
                const size_t j = size_t(sy * canvas.width + sx);
                const double c = (placement.flip ? X - sx - 1 : sx - X) + 12, r = sy - Y + 38, ri = r * 24 + c;
                const double actorC        = (placement.flip ? X - sx - 1 : sx - X) - std::floor(inputActor.x),
                             actorR        = sy - Y - std::floor(inputActor.y);
                const double actorIndex    = actorR * inputActor.width + actorC;
                const bool   inputStanding = actorC >= 0 && actorR >= 0 && actorC < inputActor.width &&
                                             actorR < inputActor.height && actorIndex == std::floor(actorIndex) &&
                                             actorIndex >= 0 && actorIndex < double(inputActor.pixels.size()) &&
                                             (inputActor.pixels[size_t(actorIndex)] >> 24) != 0;
                const bool   standing      = inputBody ? inputStanding
                                                       : c >= 0 && r >= 0 && c < 24 && r < 38 && ri == std::floor(ri) &&
                                                             ri >= 0 && ri < 912 && rest.dummy.mask[size_t(ri)] == 1;
                if (bc[j] && bc[j] != ac[j] && !standing) copy(false, sx, sy, bc[j]);
                if (bl[j] & 0xffffffu) copy(true, sx, sy, bl[j]);
            }
    }
    if (showTargets) drawActTargets(*state_->recipe->art, state_->targets, canvas, placement, true);
    const std::array<uint32_t, 4> dustColors{hex(0xefe7dc), hex(0xc9bfb8), hex(0x8f8693), hex(0x5a5468)};
    for (const auto& p : state_->specks) {
        const auto   color = p.color ? p.color : dustColors[size_t(std::min(3., std::floor(p.age / p.life * 4)))];
        const double lx = jsRound(p.x), py = Y + jsRound(p.y);
        for (int i = 0; i < (p.big ? 2 : 1); ++i)
            if (auto j = index(placement.flip ? X - lx - i - 1 : X + lx + i, py)) canvas.color[*j] = color;
    }
    return R::success(ActPreparedFrame(std::move(frame)));
}
}  // namespace eve::stylize::pixelfx
