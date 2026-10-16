#include "editor/EditorAssetDatabase.h"
#include <algorithm>
#include <cctype>
#include <exception>
#include <set>
#include <utility>
#include "common/Exception.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorDiagnostics.h"
#include "editor/EditorDock.h"
#include "settlement/SettlementRules.h"

// EditorAssetDatabase.cpp
namespace eve::editor {
namespace {

bool containsInsensitive(std::string value, std::string text) {
    auto lower = [](unsigned char character) { return static_cast<char>(std::tolower(character)); };
    std::transform(value.begin(), value.end(), value.begin(), lower);
    std::transform(text.begin(), text.end(), text.begin(), lower);
    return value.find(text) != std::string::npos;
}

}  // namespace

Result<AssetRecord> MemoryAssetDatabase::publish(AssetRecord record, std::vector<AssetDependency> dependencies) {
    if (record.guid.empty() || record.logicalUri.empty() || record.typeId.empty())
        return eve::editing::failed<AssetRecord>(EditorStatus::Rejected, RuleId("editor.asset.invalid-record"),
                                                 "Asset GUID, logical URI and type are required");
    const auto uriOwner = uriToGuid_.find(record.logicalUri);
    if (uriOwner != uriToGuid_.end() && uriOwner->second != record.guid)
        return eve::editing::failed<AssetRecord>(EditorStatus::Conflict, RuleId("editor.asset.uri-conflict"),
                                                 "Another asset already owns this logical URI");
    for (const AssetDependency& dependency : dependencies) {
        if (dependency.from != record.guid || dependency.to.empty())
            return eve::editing::failed<AssetRecord>(EditorStatus::Rejected, RuleId("editor.asset.invalid-dependency"),
                                                     "Published dependencies must originate from the product asset");
    }
    record.status       = AssetStatus::Ready;
    const auto existing = records_.find(record.guid);
    if (existing != records_.end() && existing->second.logicalUri != record.logicalUri)
        uriToGuid_.erase(existing->second.logicalUri);
    uriToGuid_.insert_or_assign(record.logicalUri, record.guid);
    records_.insert_or_assign(record.guid, record);
    std::erase_if(dependencies_, [&](const AssetDependency& dependency) { return dependency.from == record.guid; });
    dependencies_.insert(dependencies_.end(), dependencies.begin(), dependencies.end());
    ++generation_;
    return eve::editing::applied<AssetRecord>(std::move(record));
}

Result<std::vector<AssetRecord>> MemoryAssetDatabase::publishBatch(std::vector<AssetPublication> publications) {
    if (publications.empty())
        return eve::editing::failed<std::vector<AssetRecord>>(EditorStatus::Rejected,
                                                              RuleId("editor.asset.empty-publication"),
                                                              "An asset publication batch must not be empty");
    auto                                                               stagedRecords      = records_;
    auto                                                               stagedUris         = uriToGuid_;
    auto                                                               stagedDependencies = dependencies_;
    std::vector<AssetRecord>                                           published;
    std::unordered_map<AssetGuid, bool, StrongEditorIdHash<AssetGuid>> batchGuids;
    published.reserve(publications.size());
    for (AssetPublication& publication : publications) {
        AssetRecord& record = publication.record;
        if (record.guid.empty() || record.logicalUri.empty() || record.typeId.empty())
            return eve::editing::failed<std::vector<AssetRecord>>(EditorStatus::Rejected,
                                                                  RuleId("editor.asset.invalid-record"),
                                                                  "Asset GUID, logical URI and type are required");
        if (!batchGuids.emplace(record.guid, true).second)
            return eve::editing::failed<std::vector<AssetRecord>>(
                EditorStatus::Conflict, RuleId("editor.asset.duplicate-batch-guid"),
                "An asset publication batch contains a duplicate GUID");
        const auto uriOwner = stagedUris.find(record.logicalUri);
        if (uriOwner != stagedUris.end() && uriOwner->second != record.guid)
            return eve::editing::failed<std::vector<AssetRecord>>(EditorStatus::Conflict,
                                                                  RuleId("editor.asset.uri-conflict"),
                                                                  "Another asset already owns this logical URI");
        for (const AssetDependency& dependency : publication.dependencies) {
            if (dependency.from != record.guid || dependency.to.empty())
                return eve::editing::failed<std::vector<AssetRecord>>(
                    EditorStatus::Rejected, RuleId("editor.asset.invalid-dependency"),
                    "Published dependencies must originate from the product asset");
        }
        record.status       = AssetStatus::Ready;
        const auto existing = stagedRecords.find(record.guid);
        if (existing != stagedRecords.end() && existing->second.logicalUri != record.logicalUri)
            stagedUris.erase(existing->second.logicalUri);
        stagedUris.insert_or_assign(record.logicalUri, record.guid);
        stagedRecords.insert_or_assign(record.guid, record);
        std::erase_if(stagedDependencies,
                      [&](const AssetDependency& dependency) { return dependency.from == record.guid; });
        stagedDependencies.insert(stagedDependencies.end(), publication.dependencies.begin(),
                                  publication.dependencies.end());
        published.push_back(std::move(record));
    }
    records_      = std::move(stagedRecords);
    uriToGuid_    = std::move(stagedUris);
    dependencies_ = std::move(stagedDependencies);
    ++generation_;
    return eve::editing::applied<std::vector<AssetRecord>>(std::move(published));
}

Result<AssetRecord> MemoryAssetDatabase::find(const AssetGuid& guid) const {
    auto found = records_.find(guid);
    if (found == records_.end())
        return eve::editing::failed<AssetRecord>(EditorStatus::NotFound, RuleId("editor.asset.not-found"),
                                                 "Asset is not indexed: " + guid.value());
    return eve::editing::applied<AssetRecord>(found->second);
}

Result<AssetRecord> MemoryAssetDatabase::findByUri(const std::string& logicalUri) const {
    const auto guid = uriToGuid_.find(logicalUri);
    if (guid != uriToGuid_.end()) return find(guid->second);
    return eve::editing::failed<AssetRecord>(EditorStatus::NotFound, RuleId("editor.asset.not-found"),
                                             "Asset URI is not indexed: " + logicalUri);
}

Result<AssetPage<AssetRecord>> MemoryAssetDatabase::query(const AssetQuery& query, std::size_t offset,
                                                          std::size_t                  limit,
                                                          std::optional<std::uint64_t> generation) const {
    if (generation && *generation != generation_)
        return eve::editing::failed<AssetPage<AssetRecord>>(EditorStatus::Conflict, RuleId("editor.asset.page-expired"),
                                                            "Asset index changed while paging");
    std::vector<AssetRecord> matches;
    for (const auto& [guid, record] : records_) {
        (void)guid;
        if (query.status && record.status != *query.status) continue;
        if (!query.typeIds.empty() &&
            std::find(query.typeIds.begin(), query.typeIds.end(), record.typeId) == query.typeIds.end())
            continue;
        if (!query.pathPrefixes.empty()) {
            bool prefix = false;
            for (const std::string& candidate : query.pathPrefixes)
                if (record.logicalUri.starts_with(candidate)) prefix = true;
            if (!prefix) continue;
        }
        bool tags = true;
        for (const std::string& tag : query.tagsAll)
            if (std::find(record.tags.begin(), record.tags.end(), tag) == record.tags.end()) tags = false;
        if (!tags) continue;
        if (!query.text.empty() && !containsInsensitive(record.logicalUri, query.text) &&
            !containsInsensitive(record.typeId, query.text))
            continue;
        matches.push_back(record);
    }
    std::sort(matches.begin(), matches.end(),
              [](const AssetRecord& left, const AssetRecord& right) { return left.logicalUri < right.logicalUri; });
    AssetPage<AssetRecord> page;
    page.generation = generation_;
    if (offset < matches.size()) {
        const std::size_t end = std::min(matches.size(), offset + limit);
        page.values.assign(matches.begin() + static_cast<std::ptrdiff_t>(offset),
                           matches.begin() + static_cast<std::ptrdiff_t>(end));
        page.nextOffset = end;
        page.hasMore    = end < matches.size();
    }
    return eve::editing::applied<AssetPage<AssetRecord>>(std::move(page));
}

std::vector<AssetDependency> MemoryAssetDatabase::dependencies(const AssetGuid& guid, bool incoming) const {
    std::vector<AssetDependency> result;
    for (const AssetDependency& dependency : dependencies_)
        if ((incoming && dependency.to == guid) || (!incoming && dependency.from == guid)) result.push_back(dependency);
    return result;
}

Result<AssetRecord> ImportCoordinator::publish(ImportProduct product) {
    if (!database_)
        return eve::editing::failed<AssetRecord>(EditorStatus::Failed, RuleId("editor.import.missing-database"),
                                                 "Import coordinator has no asset database");
    if (product.record.sourceUri.empty() || product.record.importerId.empty())
        return eve::editing::failed<AssetRecord>(EditorStatus::Rejected, RuleId("editor.import.invalid-product"),
                                                 "Import product requires source URI and importer identity");
    return database_->publish(std::move(product.record), std::move(product.dependencies));
}

Result<ImportTicket> ImportCoordinator::begin(const AssetGuid& asset, std::string sourceHash, std::string importerId,
                                              std::uint32_t importerVersion) {
    if (asset.empty() || sourceHash.empty() || importerId.empty() || importerVersion == 0)
        return eve::editing::failed<ImportTicket>(EditorStatus::Rejected, RuleId("editor.import.invalid-request"),
                                                  "Import request requires asset, source hash and versioned importer");
    const std::uint64_t generation = ++generations_[asset];
    return eve::editing::applied<ImportTicket>(
        {asset, generation, std::move(sourceHash), std::move(importerId), importerVersion});
}

Result<AssetRecord> ImportCoordinator::publish(const ImportTicket& ticket, ImportProduct product) {
    if (!database_)
        return eve::editing::failed<AssetRecord>(EditorStatus::Failed, RuleId("editor.import.missing-database"),
                                                 "Import coordinator has no asset database");
    const auto generation = generations_.find(ticket.asset);
    if (generation == generations_.end() || generation->second != ticket.generation)
        return eve::editing::failed<AssetRecord>(EditorStatus::Conflict, RuleId("editor.import.stale-product"),
                                                 "Importer product was superseded by a newer source generation");
    AssetRecord& record = product.record;
    if (record.guid != ticket.asset || record.sourceHash != ticket.sourceHash ||
        record.importerId != ticket.importerId || record.importerVersion != ticket.importerVersion)
        return eve::editing::failed<AssetRecord>(EditorStatus::Conflict, RuleId("editor.import.ticket-mismatch"),
                                                 "Importer product identity does not match its request ticket");
    if (record.sourceUri.empty() || record.logicalUri.empty() || record.typeId.empty())
        return eve::editing::failed<AssetRecord>(EditorStatus::Rejected, RuleId("editor.import.incomplete-product"),
                                                 "Importer product lacks source, logical URI or asset type");
    std::set<std::string> artifacts;
    for (const std::string& artifact : record.artifacts)
        if (artifact.empty() || !artifacts.insert(artifact).second)
            return eve::editing::failed<AssetRecord>(EditorStatus::Rejected, RuleId("editor.import.invalid-artifact"),
                                                     "Importer artifacts must be unique and non-empty");
    if (record.artifacts.empty())
        return eve::editing::failed<AssetRecord>(EditorStatus::Rejected, RuleId("editor.import.missing-artifact"),
                                                 "Importer product must contain at least one published artifact");
    if (std::any_of(record.diagnostics.begin(), record.diagnostics.end(), [](const EditorDiagnostic& diagnostic) {
            return diagnostic.severity() == DiagnosticSeverity::Error ||
                   diagnostic.severity() == DiagnosticSeverity::Fatal;
        }))
        return eve::editing::failed<AssetRecord>(EditorStatus::Rejected, RuleId("editor.import.product-errors"),
                                                 "Importer product contains error diagnostics");
    auto published = database_->publish(std::move(record), std::move(product.dependencies));
    if (published.ok()) generations_.erase(ticket.asset);
    return published;
}

}  // namespace eve::editor

// EditorCommandService.cpp
namespace eve::editor {

namespace {

constexpr size_t kMaxPayloadDepth    = 32;
constexpr size_t kMaxPayloadElements = 100'000;

}  // namespace

Result<EditorValue> EditorCommandService::error(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<EditorValue>(status, RuleId(rule), std::move(message));
}

Result<void> EditorCommandService::checkExecutionPolicy(const CommandDescriptor& descriptor, const CommandId& id,
                                                        CommandSource source, const EditorValue& payload,
                                                        const HostProfile& profile) const {
    if (!profile.allowsCommand(id))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.profile-denied"),
                                          "Command is unavailable in this host: " + id.value());
    if (!profile.hasFeatures(descriptor.requiredFeatures))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.feature-denied"),
                                          "Command requires features unavailable in this host: " + id.value());
    if (source == CommandSource::Automation && !descriptor.automationAllowed)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.automation-denied"),
                                          "Command is not available to automation: " + id.value());
    if (!payload.isWithinLimits(kMaxPayloadDepth, kMaxPayloadElements, profile.maxPayloadBytes()))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.payload-limit"),
                                          "Command payload exceeds host limits: " + id.value());
    return eve::editing::applied<void>();
}

Result<EditorValue> EditorCommandService::registerCommand(CommandDescriptor descriptor, EditorCommandHandler handler,
                                                          bool replace) {
    if (!descriptor.id || descriptor.ownerModule.empty() || !handler)
        return error(EditorStatus::Rejected, "editor.command.invalid-registration",
                     "Command id, owner module and handler are required");

    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == descriptor.id; });
    if (it != commands_.end()) {
        if (!replace || it->descriptor.ownerModule != descriptor.ownerModule)
            return error(EditorStatus::Rejected, "editor.command.duplicate",
                         "Command is already registered: " + descriptor.id.value());
        *it = {std::move(descriptor), std::move(handler), {}, {}, nextRegistrationGeneration_++};
    } else {
        commands_.push_back({std::move(descriptor), std::move(handler), {}, {}, nextRegistrationGeneration_++});
    }
    ++revision_;
    return eve::editing::applied<EditorValue>(EditorValue{});
}

Result<EditorValue> EditorCommandService::registerPlannedCommand(CommandDescriptor         descriptor,
                                                                 EditorCommandPlanner      planner,
                                                                 EditorCommandPlanExecutor executor, bool replace) {
    if (!descriptor.id || descriptor.ownerModule.empty() || !planner || !executor)
        return error(EditorStatus::Rejected, "editor.command.invalid-registration",
                     "Command id, owner module, planner and executor are required");
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == descriptor.id; });
    if (it != commands_.end()) {
        if (!replace || it->descriptor.ownerModule != descriptor.ownerModule)
            return error(EditorStatus::Rejected, "editor.command.duplicate",
                         "Command is already registered: " + descriptor.id.value());
        *it = {std::move(descriptor), {}, std::move(planner), std::move(executor), nextRegistrationGeneration_++};
    } else {
        commands_.push_back(
            {std::move(descriptor), {}, std::move(planner), std::move(executor), nextRegistrationGeneration_++});
    }
    ++revision_;
    return eve::editing::applied<EditorValue>(EditorValue{});
}

bool EditorCommandService::unregisterCommand(const CommandId& id, const std::string& ownerModule) {
    auto it = std::find_if(commands_.begin(), commands_.end(), [&](const Registration& entry) {
        return entry.descriptor.id == id && (ownerModule.empty() || entry.descriptor.ownerModule == ownerModule);
    });
    if (it == commands_.end()) return false;
    commands_.erase(it);
    ++revision_;
    return true;
}

size_t EditorCommandService::unregisterOwner(const std::string& ownerModule) {
    if (ownerModule.empty()) return 0;
    const size_t before = commands_.size();
    std::erase_if(commands_, [&](const Registration& entry) { return entry.descriptor.ownerModule == ownerModule; });
    const size_t removed = before - commands_.size();
    if (removed > 0) ++revision_;
    return removed;
}

void EditorCommandService::clear() {
    if (commands_.empty()) return;
    commands_.clear();
    ++revision_;
}

const CommandDescriptor* EditorCommandService::find(const CommandId& id) const {
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == id; });
    return it == commands_.end() ? nullptr : &it->descriptor;
}

bool EditorCommandService::supportsPlanning(const CommandId& id) const {
    const auto it = std::find_if(commands_.begin(), commands_.end(),
                                 [&](const Registration& entry) { return entry.descriptor.id == id; });
    return it != commands_.end() && static_cast<bool>(it->planner) && static_cast<bool>(it->planExecutor);
}

std::vector<CommandDescriptor> EditorCommandService::commands(const HostProfile& profile) const {
    std::vector<CommandDescriptor> result;
    result.reserve(commands_.size());
    for (const auto& entry : commands_)
        if (profile.allowsCommand(entry.descriptor.id) && profile.hasFeatures(entry.descriptor.requiredFeatures))
            result.push_back(entry.descriptor);
    return result;
}

Result<EditorValue> EditorCommandService::execute(const CommandId& id, const CommandContext& context,
                                                  const EditorValue& payload) const {
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == id; });
    if (it == commands_.end())
        return error(EditorStatus::NotFound, "editor.command.not-found", "Command is not registered: " + id.value());
    if (!context.profile)
        return error(EditorStatus::Failed, "editor.command.missing-profile", "Command context has no host profile");
    if (auto policy = checkExecutionPolicy(it->descriptor, id, context.source, payload, *context.profile); !policy.ok())
        return Result<EditorValue>::failure(std::move(policy).status());

    if (!it->handler)
        return error(EditorStatus::Unsupported, "editor.command.requires-plan",
                     "Command must be planned before it can be executed: " + id.value());
    try {
        return it->handler(context, payload);
    } catch (const std::exception& exception) {
        return error(EditorStatus::Failed, "editor.command.handler-exception",
                     "Command handler failed: " + std::string(exception.what()));
    } catch (...) {
        return error(EditorStatus::Failed, "editor.command.handler-exception",
                     "Command handler failed with an unknown exception");
    }
}

Result<CommandPlan> EditorCommandService::plan(const CommandRequest& request, const HostProfile& profile) const {
    auto failure = [](EditorStatus status, const char* rule, std::string message) {
        return eve::editing::failed<CommandPlan>(status, RuleId(rule), std::move(message));
    };
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == request.id; });
    if (it == commands_.end())
        return failure(EditorStatus::NotFound, "editor.command.not-found",
                       "Command is not registered: " + request.id.value());
    if (auto policy = checkExecutionPolicy(it->descriptor, request.id, request.source, request.payload, profile);
        !policy.ok())
        return Result<CommandPlan>::failure(std::move(policy).status());
    if (request.expectedRevision && *request.expectedRevision != request.context.targetRevision)
        return failure(EditorStatus::Conflict, "editor.command.revision-conflict",
                       "Expected revision does not match the captured context");
    if (!it->planner)
        return failure(EditorStatus::Unsupported, "editor.command.planning-unsupported",
                       "Command does not provide a planning handler");
    try {
        Result<CommandPlan> result = it->planner(request);
        if (result.ok()) {
            result.value().command      = request.id;
            result.value().target       = request.context.target;
            result.value().baseRevision = request.context.targetRevision;
            if (result.value().targetGeneration == 0)
                result.value().targetGeneration = request.context.targetGeneration;
            result.value().commandGeneration = it->generation;
            result.value().plannedPayload    = request.payload;
            result.value().plannedSource     = request.source;
            if (result.value().id.empty())
                result.value().id = PlanId(request.id.value() + ".plan." + std::to_string(++planSequence_));
        }
        return result;
    } catch (const std::exception& exception) {
        return failure(EditorStatus::Failed, "editor.command.planner-exception", exception.what());
    } catch (...) {
        return failure(EditorStatus::Failed, "editor.command.planner-exception",
                       "Command planner failed with an unknown exception");
    }
}

Result<TransactionReceipt> EditorCommandService::executePlan(const CommandRequest& request, const CommandPlan& plan,
                                                             const HostProfile& profile) const {
    auto failure = [](EditorStatus status, const char* rule, std::string message) {
        return eve::editing::failed<TransactionReceipt>(status, RuleId(rule), std::move(message));
    };
    if (plan.id.empty() || plan.command != request.id || plan.target != request.context.target)
        return failure(EditorStatus::Rejected, "editor.command.invalid-plan",
                       "Plan identity or target does not match the request");
    if (plan.baseRevision != request.context.targetRevision)
        return failure(EditorStatus::Conflict, "editor.command.stale-plan",
                       "Plan was produced for another target revision");
    if (plan.targetGeneration != request.context.targetGeneration)
        return failure(EditorStatus::Conflict, "editor.command.stale-target-generation",
                       "Plan was produced for another target lifetime");
    if (request.expectedRevision && *request.expectedRevision != request.context.targetRevision)
        return failure(EditorStatus::Conflict, "editor.command.revision-conflict",
                       "Expected revision does not match the captured context");
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == request.id; });
    if (it == commands_.end())
        return failure(EditorStatus::NotFound, "editor.command.not-found",
                       "Command is not registered: " + request.id.value());
    if (plan.commandGeneration != it->generation)
        return failure(EditorStatus::Conflict, "editor.command.stale-registration",
                       "Command registration changed after planning");
    if (auto policy = checkExecutionPolicy(it->descriptor, request.id, request.source, request.payload, profile);
        !policy.ok())
        return Result<TransactionReceipt>::failure(std::move(policy).status());
    if (request.payload != plan.plannedPayload || request.source != plan.plannedSource)
        return failure(EditorStatus::Rejected, "editor.command.plan-input-mismatch",
                       "Plan payload or source differs from the validated request");
    if (!it->planExecutor)
        return failure(EditorStatus::Unsupported, "editor.command.execution-unsupported",
                       "Command does not provide a plan executor");
    try {
        return it->planExecutor(request, plan);
    } catch (const std::exception& exception) {
        return failure(EditorStatus::Failed, "editor.command.executor-exception", exception.what());
    } catch (...) {
        return failure(EditorStatus::Failed, "editor.command.executor-exception",
                       "Command executor failed with an unknown exception");
    }
}

}  // namespace eve::editor

// EditorDiagnostics.cpp
namespace eve::editor {

EditorValidationService::EditorValidationService() {
    rules_.emplace(RuleId("settlement.rules.document"),
                   RegisteredRule{"settlement", [](const ValidationRequest& request) {
                                      if (request.subject != "settlement.rules") return std::vector<EditorDiagnostic>{};
                                      const auto* json = request.value.getIf<std::string>();
                                      if (json == nullptr)
                                          return std::vector<EditorDiagnostic>{eve::editing::ruleDiagnostic(
                                              eve::DiagnosticCode::TypeMismatch, RuleId("settlement.rules.document"),
                                              DiagnosticSeverity::Error,
                                              "Settlement rule document must be supplied as JSON text")};
                                      auto inspected = settlement::SettlementRuleSet::fromJson(*json);
                                      if (inspected) return std::vector<EditorDiagnostic>{};
                                      return inspected.status().diagnostics();
                                  }});
}

Result<void> EditorValidationService::registerRule(std::string owner, RuleId id, Rule rule) {
    if (owner.empty() || id.empty() || !rule)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.validation.invalid-rule"),
                                          "Validation owner, rule id and callback are required");
    rules_.insert_or_assign(std::move(id), RegisteredRule{std::move(owner), std::move(rule)});
    return eve::editing::applied<void>();
}

std::size_t EditorValidationService::unregisterOwner(const std::string& owner) {
    return std::erase_if(rules_, [&](const auto& entry) { return entry.second.owner == owner; });
}

std::vector<EditorDiagnostic> EditorValidationService::validate(const ValidationRequest& request) const {
    std::vector<EditorDiagnostic> diagnostics;
    for (const auto& [id, registered] : rules_) {
        (void)id;
        auto current = registered.rule(request);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(current.begin()),
                           std::make_move_iterator(current.end()));
    }
    return diagnostics;
}

void EditorDiagnosticService::publish(std::string source, std::vector<EditorDiagnostic> diagnostics) {
    std::lock_guard lock(mutex_);
    channels_.insert_or_assign(std::move(source), std::move(diagnostics));
}

void EditorDiagnosticService::clear(const std::string& source) {
    std::lock_guard lock(mutex_);
    channels_.erase(source);
}

std::vector<EditorDiagnostic> EditorDiagnosticService::snapshot() const {
    std::lock_guard               lock(mutex_);
    std::vector<EditorDiagnostic> result;
    for (const auto& [source, diagnostics] : channels_) {
        (void)source;
        result.insert(result.end(), diagnostics.begin(), diagnostics.end());
    }
    return result;
}

}  // namespace eve::editor

// EditorDock.cpp
namespace eve::editor {

EditorDock::EditorDock() {
    sizes_["left"]   = 200.f;
    sizes_["right"]  = 280.f;
    sizes_["top"]    = 40.f;
    sizes_["bottom"] = 24.f;
}

void EditorDock::setRegionSize(const std::string& region, float pixels) {
    if (region != "left" && region != "right" && region != "top" && region != "bottom") {
        throw Exception("EditorDock::setRegionSize: expected left|right|top|bottom");
    }
    if (pixels < 0.f) throw Exception("EditorDock::setRegionSize: size must be >= 0");
    sizes_[region] = pixels;
}

float EditorDock::getRegionSize(const std::string& region) const { return sizeOf(region); }

float EditorDock::sizeOf(const std::string& region) const {
    auto it = sizes_.find(region);
    return it == sizes_.end() ? 0.f : it->second;
}

void EditorDock::layout(float screenW, float screenH) {
    if (screenW <= 0.f || screenH <= 0.f) {
        throw Exception("EditorDock::layout: screen size must be > 0");
    }
    float left   = sizeOf("left");
    float right  = sizeOf("right");
    float top    = sizeOf("top");
    float bottom = sizeOf("bottom");
    // Clamp so center remains non-negative
    if (left + right > screenW) {
        float s = screenW / (left + right);
        left *= s;
        right *= s;
    }
    if (top + bottom > screenH) {
        float s = screenH / (top + bottom);
        top *= s;
        bottom *= s;
    }

    rects_["top"]    = {0.f, 0.f, screenW, top};
    rects_["bottom"] = {0.f, screenH - bottom, screenW, bottom};
    rects_["left"]   = {0.f, top, left, screenH - top - bottom};
    rects_["right"]  = {screenW - right, top, right, screenH - top - bottom};
    rects_["center"] = {left, top, screenW - left - right, screenH - top - bottom};
}

const EditorDock::Rect* EditorDock::rectOf(const std::string& region) const {
    auto it = rects_.find(region);
    return it == rects_.end() ? nullptr : &it->second;
}

float EditorDock::getRegionX(const std::string& region) const {
    const Rect* r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionX: call layout() first / unknown region");
    return r->x;
}
float EditorDock::getRegionY(const std::string& region) const {
    const Rect* r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionY: call layout() first / unknown region");
    return r->y;
}
float EditorDock::getRegionW(const std::string& region) const {
    const Rect* r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionW: call layout() first / unknown region");
    return r->w;
}
float EditorDock::getRegionH(const std::string& region) const {
    const Rect* r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionH: call layout() first / unknown region");
    return r->h;
}

}  // namespace eve::editor
