#include "rpg/Settlement.h"

#include "settlement/Settlement.h"

#include <algorithm>

namespace eve::rpg {

double SettlementContext::get(const std::string &key, double fallback) const {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

void SettlementContext::set(const std::string &key, double value) { values[key] = value; }

bool SettlementContext::has(const std::string &key) const { return values.find(key) != values.end(); }

void SettlementContext::addTag(const std::string &tag) {
    if (!hasTag(tag)) tags.push_back(tag);
}

bool SettlementContext::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

namespace {

struct StageEntry {
    std::string name;
    int priority = 0;
    bool enabled = true;
    SettlementPipeline::Stage fn;
};

std::unordered_map<std::string, std::vector<StageEntry>> &pipelines() {
    static std::unordered_map<std::string, std::vector<StageEntry>> p;
    return p;
}

std::vector<StageEntry> *findPipeline(const std::string &pipeline) {
    auto it = pipelines().find(pipeline);
    return it == pipelines().end() ? nullptr : &it->second;
}

class CompatibilitySettlementPolicy final : public settlement::ISettlementPolicy {
public:
    eve::Result<void> validate(settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> sourceModifiers(settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> clamp(settlement::SettlementContext& context) override {
        return context.setClampMax(context.magnitude());
    }
    eve::Result<settlement::PreparedApply> prepareApply(const settlement::SettlementContext&) override {
        return eve::Result<settlement::PreparedApply>::success(
            settlement::PreparedApply([] { return eve::Result<void>::success(); }, [] {}));
    }
};

SubjectRef compatibilityTarget() {
    PersistentId::Bytes bytes{};
    bytes[0]  = 0x52;
    bytes[1]  = 0x50;
    bytes[2]  = 0x47;
    bytes[15] = 1;
    return SubjectRef::fromPersistentId(PersistentId(bytes));
}

StageEntry *findStage(std::vector<StageEntry> &stages, const std::string &name) {
    auto it = std::find_if(stages.begin(), stages.end(),
                            [&](const StageEntry &s) { return s.name == name; });
    return it == stages.end() ? nullptr : &(*it);
}

}  // namespace

void SettlementPipeline::registerStage(const std::string &pipeline, const std::string &stage,
                                        int priority, Stage fn) {
    auto &stages = pipelines()[pipeline];
    if (StageEntry *existing = findStage(stages, stage)) {
        existing->priority = priority;
        existing->fn = std::move(fn);
        existing->enabled = true;
        return;
    }
    StageEntry entry;
    entry.name = stage;
    entry.priority = priority;
    entry.enabled = true;
    entry.fn = std::move(fn);
    stages.push_back(std::move(entry));
}

bool SettlementPipeline::unregisterStage(const std::string &pipeline, const std::string &stage) {
    auto *stages = findPipeline(pipeline);
    if (!stages) return false;
    size_t before = stages->size();
    stages->erase(std::remove_if(stages->begin(), stages->end(),
                                  [&](const StageEntry &s) { return s.name == stage; }),
                  stages->end());
    return stages->size() != before;
}

bool SettlementPipeline::setStageEnabled(const std::string &pipeline, const std::string &stage,
                                          bool enabled) {
    auto *stages = findPipeline(pipeline);
    if (!stages) return false;
    StageEntry *s = findStage(*stages, stage);
    if (!s) return false;
    s->enabled = enabled;
    return true;
}

bool SettlementPipeline::setStagePriority(const std::string &pipeline, const std::string &stage,
                                           int priority) {
    auto *stages = findPipeline(pipeline);
    if (!stages) return false;
    StageEntry *s = findStage(*stages, stage);
    if (!s) return false;
    s->priority = priority;
    return true;
}

bool SettlementPipeline::hasStage(const std::string &pipeline, const std::string &stage) {
    auto *stages = findPipeline(pipeline);
    if (!stages) return false;
    return findStage(*stages, stage) != nullptr;
}

int SettlementPipeline::stageCount(const std::string &pipeline) {
    auto *stages = findPipeline(pipeline);
    return stages ? int(stages->size()) : 0;
}

void SettlementPipeline::clearPipeline(const std::string &pipeline) { pipelines().erase(pipeline); }

void SettlementPipeline::run(const std::string &pipeline, SettlementContext &ctx) {
    auto *stages = findPipeline(pipeline);
    if (!stages) return;

    settlement::SettlementPipeline canonical;
    auto installed = canonical.addStage(
        settlement::StageKind::SourceModifiers, "rpg.compatibility", 0,
        [stages, &ctx](settlement::SettlementContext&) {
            std::vector<StageEntry *> ordered;
            ordered.reserve(stages->size());
            for (auto &stage : *stages)
                if (stage.enabled) ordered.push_back(&stage);
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const StageEntry *left, const StageEntry *right) {
                                 return left->priority < right->priority;
                             });
            for (StageEntry *stage : ordered) {
                if (ctx.cancelled) break;
                if (stage->fn) stage->fn(ctx);
            }
            return eve::Result<void>::success();
        });
    if (!installed) {
        installed.ignore("RPG compatibility settlement stage installation");
        return;
    }

    settlement::SettlementRequest request;
    request.target    = compatibilityTarget();
    request.kind      = ctx.kind.empty() ? "rpg.compatibility" : ctx.kind;
    request.resource  = "legacy.values";
    request.magnitude = 0.0;
    CompatibilitySettlementPolicy policy;
    auto settled = canonical.settle(request, policy);
    settled.ignore("RPG compatibility settlement execution");
}

}  // namespace eve::rpg
