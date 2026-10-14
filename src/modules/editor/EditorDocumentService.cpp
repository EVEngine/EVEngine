#include "editor/EditorDocumentService.h"
#include <algorithm>
#include <exception>
#include <set>
#include <utility>
#include "editor/EditorEnvironmentBrowser.h"
#include "editor/EditorEventTimeline.h"
#include "editor/EditorExtension.h"
#include "editor/EditorValueJson.h"

// EditorDocumentService.cpp
namespace eve::editor {

Result<StoredDocument> MemoryAtomicDocumentStore::read(const std::string& resourceUri) const {
    auto found = documents_.find(resourceUri);
    if (found == documents_.end())
        return eve::editing::failed<StoredDocument>(EditorStatus::NotFound, RuleId("editor.document.store-not-found"),
                                                    "Document resource is not persisted: " + resourceUri);
    return eve::editing::applied<StoredDocument>(found->second);
}

Result<StoredDocument> MemoryAtomicDocumentStore::compareAndSwap(const std::string& resourceUri,
                                                                 Revision           expectedRevision,
                                                                 const std::string& expectedContentHash,
                                                                 const EditorValue& content) {
    auto              found       = documents_.find(resourceUri);
    const Revision    current     = found == documents_.end() ? 0 : found->second.revision;
    const std::string currentHash = found == documents_.end() ? std::string{} : found->second.contentHash;
    if (current != expectedRevision || currentHash != expectedContentHash)
        return eve::editing::failed<StoredDocument>(EditorStatus::Conflict, RuleId("editor.document.disk-conflict"),
                                                    "Document store revision changed before save");
    StoredDocument stored;
    stored.content     = content;
    stored.revision    = current + 1;
    stored.contentHash = editorValueContentHash(content);
    documents_.insert_or_assign(resourceUri, stored);
    return eve::editing::applied<StoredDocument>(std::move(stored));
}

Result<DocumentSnapshot> DocumentService::open(DocumentKey key, std::string title, std::string resourceUri,
                                               EditorValue initialContent) {
    if (!store_ || key.asset.empty() || resourceUri.empty())
        return error(EditorStatus::Rejected, "editor.document.invalid-open",
                     "Document store, asset identity and resource URI are required");
    DocumentId id("document:" + key.asset.value());
    if (open_.contains(id)) return eve::editing::applied<DocumentSnapshot>(open_.at(id).snapshot);

    OpenDocument document;
    document.snapshot.id          = id;
    document.snapshot.key         = std::move(key);
    document.snapshot.title       = std::move(title);
    document.snapshot.resourceUri = std::move(resourceUri);
    document.snapshot.state       = DocumentState::Ready;
    Result<StoredDocument> stored = store_->read(document.snapshot.resourceUri);
    if (stored.ok()) {
        document.content                  = stored.value().content;
        document.snapshot.revision.disk   = stored.value().revision;
        document.snapshot.revision.edit   = stored.value().revision;
        document.snapshot.revision.saved  = stored.value().revision;
        document.snapshot.diskContentHash = stored.value().contentHash;
    } else if (stored.code() == EditorStatus::NotFound) {
        document.content = std::move(initialContent);
    } else {
        return error(stored.code(), "editor.document.store-read", "Document store could not be read");
    }
    DocumentSnapshot snapshot = document.snapshot;
    open_.emplace(id, std::move(document));
    return eve::editing::applied<DocumentSnapshot>(std::move(snapshot));
}

Result<DocumentSnapshot> DocumentService::edit(const DocumentId& document, EditorValue content,
                                               std::optional<Revision> expectedRevision) {
    auto found = open_.find(document);
    if (found == open_.end()) return error(EditorStatus::NotFound, "editor.document.not-open", "Document is not open");
    if (expectedRevision && *expectedRevision != found->second.snapshot.revision.edit)
        return error(EditorStatus::Conflict, "editor.document.edit-conflict",
                     "Document edit revision changed before mutation");
    found->second.content = std::move(content);
    ++found->second.snapshot.revision.edit;
    found->second.snapshot.state = DocumentState::Ready;
    return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
}

Result<SaveTicket> DocumentService::requestSave(const DocumentId& document) {
    auto found = open_.find(document);
    if (found == open_.end())
        return eve::editing::failed<SaveTicket>(EditorStatus::NotFound, RuleId("editor.document.not-open"),
                                                "Document is not open");
    SaveTicket ticket;
    ticket.id                   = StableId(document.value() + ".save." + std::to_string(++ticketSequence_));
    ticket.document             = document;
    ticket.capturedEditRevision = found->second.snapshot.revision.edit;
    ticket.expectedDiskRevision = found->second.snapshot.revision.disk;
    ticket.expectedContentHash  = found->second.snapshot.diskContentHash;
    pendingSaves_.emplace(ticket.id, PendingSave{ticket, found->second.content});
    return eve::editing::applied<SaveTicket>(std::move(ticket));
}

Result<DocumentSnapshot> DocumentService::executeSave(const SaveTicket& ticket) {
    auto pending  = pendingSaves_.find(ticket.id);
    auto document = open_.find(ticket.document);
    if (pending == pendingSaves_.end() || document == open_.end())
        return error(EditorStatus::NotFound, "editor.document.save-ticket-not-found",
                     "Save ticket or document is no longer available");
    document->second.snapshot.state = DocumentState::Saving;
    Result<StoredDocument> stored =
        store_->compareAndSwap(document->second.snapshot.resourceUri, ticket.expectedDiskRevision,
                               ticket.expectedContentHash, pending->second.content);
    pendingSaves_.erase(pending);
    if (!stored.ok()) {
        document->second.snapshot.state =
            stored.code() == EditorStatus::Conflict ? DocumentState::Conflict : DocumentState::Failed;
        document->second.snapshot.diagnostics = stored.diagnostics();
        return Result<DocumentSnapshot>::failure(stored.status());
    }
    document->second.snapshot.revision.disk   = stored.value().revision;
    document->second.snapshot.revision.saved  = ticket.capturedEditRevision;
    document->second.snapshot.diskContentHash = stored.value().contentHash;
    document->second.snapshot.state           = DocumentState::Ready;
    document->second.snapshot.diagnostics.clear();
    return eve::editing::applied<DocumentSnapshot>(document->second.snapshot);
}

Result<EditorValue> DocumentService::content(const DocumentId& document) const {
    auto found = open_.find(document);
    if (found == open_.end())
        return eve::editing::failed<EditorValue>(EditorStatus::NotFound, RuleId("editor.document.not-open"),
                                                 "Document is not open");
    return eve::editing::applied<EditorValue>(found->second.content);
}

Result<DocumentSnapshot> DocumentService::snapshot(const DocumentId& document) const {
    auto found = open_.find(document);
    if (found == open_.end()) return error(EditorStatus::NotFound, "editor.document.not-open", "Document is not open");
    return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
}

std::vector<DocumentSnapshot> DocumentService::documents() const {
    std::vector<DocumentSnapshot> result;
    result.reserve(open_.size());
    for (const auto& [id, document] : open_) {
        (void)id;
        result.push_back(document.snapshot);
    }
    std::sort(result.begin(), result.end(),
              [](const DocumentSnapshot& left, const DocumentSnapshot& right) { return left.id < right.id; });
    return result;
}

Result<DocumentSnapshot> DocumentService::reconcileExternal(const DocumentId& document) {
    return reconcileExternal(document, {});
}

Result<DocumentSnapshot> DocumentService::reconcileExternal(const DocumentId& document, ContentValidator validator) {
    auto found = open_.find(document);
    if (found == open_.end()) return error(EditorStatus::NotFound, "editor.document.not-open", "Document is not open");

    Result<StoredDocument> stored = store_->read(found->second.snapshot.resourceUri);
    if (!stored.ok()) {
        const bool expectedMissing = stored.code() == EditorStatus::NotFound &&
                                     found->second.snapshot.revision.disk == 0 &&
                                     found->second.snapshot.diskContentHash.empty();
        if (expectedMissing) return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
        found->second.snapshot.state =
            stored.code() == EditorStatus::NotFound || stored.code() == EditorStatus::Conflict ? DocumentState::Conflict
                                                                                               : DocumentState::Failed;
        found->second.snapshot.diagnostics = stored.diagnostics();
        if (found->second.snapshot.diagnostics.empty())
            found->second.snapshot.diagnostics.push_back(
                eve::editing::ruleDiagnostic(eve::DiagnosticCode::NotFound, RuleId("editor.document.external-missing"),
                                             DiagnosticSeverity::Error, "Persisted document was removed externally"));
        const EditorStatus status =
            found->second.snapshot.state == DocumentState::Conflict ? EditorStatus::Conflict : stored.code();
        return Result<DocumentSnapshot>::failure(eve::Status(status, found->second.snapshot.diagnostics));
    }

    const bool changed = stored.value().revision != found->second.snapshot.revision.disk ||
                         stored.value().contentHash != found->second.snapshot.diskContentHash;
    if (!changed) {
        if (found->second.snapshot.state == DocumentState::Conflict) {
            found->second.snapshot.state = DocumentState::Ready;
            found->second.snapshot.diagnostics.clear();
        }
        return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
    }
    if (found->second.snapshot.dirty()) {
        found->second.snapshot.state       = DocumentState::Conflict;
        found->second.snapshot.diagnostics = {eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::Conflict, RuleId("editor.document.external-conflict"), DiagnosticSeverity::Error,
            "Document changed on disk while the session has unsaved edits")};
        return Result<DocumentSnapshot>::failure(
            eve::Status(EditorStatus::Conflict, found->second.snapshot.diagnostics));
    }

    if (validator) {
        auto validated = validator(stored.value().content);
        if (!validated.ok()) {
            found->second.snapshot.state       = DocumentState::Conflict;
            found->second.snapshot.diagnostics = validated.diagnostics();
            if (found->second.snapshot.diagnostics.empty())
                found->second.snapshot.diagnostics.push_back(eve::editing::ruleDiagnostic(
                    eve::DiagnosticCode::InvalidArgument, RuleId("editor.document.external-invalid"),
                    DiagnosticSeverity::Error, "External document content failed domain validation"));
            return Result<DocumentSnapshot>::failure(
                eve::Status(EditorStatus::Conflict, found->second.snapshot.diagnostics));
        }
    }

    found->second.content                 = stored.value().content;
    const Revision synchronizedEdit       = std::max(found->second.snapshot.revision.edit + 1, stored.value().revision);
    found->second.snapshot.revision.edit  = synchronizedEdit;
    found->second.snapshot.revision.saved = synchronizedEdit;
    found->second.snapshot.revision.disk  = stored.value().revision;
    found->second.snapshot.diskContentHash = stored.value().contentHash;
    found->second.snapshot.state           = DocumentState::Ready;
    found->second.snapshot.diagnostics.clear();
    return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
}

Result<void> DocumentService::close(const DocumentId& document) {
    if (!open_.erase(document))
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.document.not-open"),
                                          "Document is not open");
    std::erase_if(pendingSaves_, [&](const auto& entry) { return entry.second.ticket.document == document; });
    return eve::editing::applied<void>();
}

Result<DocumentSnapshot> DocumentService::error(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<DocumentSnapshot>(status, RuleId(rule), std::move(message));
}

}  // namespace eve::editor

// EditorEnvironmentBrowser.cpp
namespace eve::editor {
namespace {
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
}  // namespace

Result<EnvironmentAssetCard> EnvironmentAssetBrowser::card(const AssetRecord& record) const {
    EnvironmentAssetCard result;
    result.asset       = record.guid;
    result.logicalUri  = record.logicalUri;
    result.status      = record.status;
    result.diagnostics = record.diagnostics;
    if (!record.artifacts.empty()) result.previewArtifact = record.artifacts.front();
    const auto*                        layoutEntry = field(record.metadata, "layout");
    const auto*                        widthEntry  = field(record.metadata, "width");
    const auto*                        heightEntry = field(record.metadata, "height");
    const auto*                        facesEntry  = field(record.metadata, "faceCount");
    const auto*                        layout      = layoutEntry ? layoutEntry->getIf<std::string>() : nullptr;
    const auto*                        width       = widthEntry ? widthEntry->getIf<int64_t>() : nullptr;
    const auto*                        height      = heightEntry ? heightEntry->getIf<int64_t>() : nullptr;
    const auto*                        faces       = facesEntry ? facesEntry->getIf<int64_t>() : nullptr;
    static const std::set<std::string> layouts{"cubemap", "horizontal-cross", "vertical-cross", "equirectangular"};
    if (!layout || !layouts.contains(*layout) || !width || !height || *width <= 0 || *height <= 0) {
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, RuleId("editor.environment.invalid-map-metadata"),
            DiagnosticSeverity::Error, "Environment map requires supported layout and positive dimensions"));
    } else {
        result.layout    = *layout;
        result.width     = static_cast<int>(*width);
        result.height    = static_cast<int>(*height);
        result.faceCount = faces ? static_cast<int>(*faces) : (*layout == "cubemap" ? 6 : 1);
        if (*layout == "cubemap" && result.faceCount != 6)
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvalidArgument, RuleId("editor.environment.cubemap-face-count"),
                DiagnosticSeverity::Error, "Cubemap assets require exactly six faces"));
        if (*layout == "equirectangular" && *width != *height * 2)
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvalidArgument, RuleId("editor.environment.equirectangular-aspect"),
                DiagnosticSeverity::Warning, "Equirectangular maps normally use a 2:1 aspect ratio"));
    }
    if (record.artifacts.empty())
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.environment.missing-artifact"),
            DiagnosticSeverity::Error, "Environment asset has no imported artifact"));
    if (record.status != AssetStatus::Ready && record.status != AssetStatus::Warning)
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.environment.asset-not-ready"),
            DiagnosticSeverity::Error, "Environment asset is not ready for preview or assignment"));
    const bool error =
        std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const EditorDiagnostic& diagnostic) {
            return diagnostic.severity() == DiagnosticSeverity::Error ||
                   diagnostic.severity() == DiagnosticSeverity::Fatal;
        });
    if (error) return Result<EnvironmentAssetCard>::failure(eve::Status(EditorStatus::Rejected, result.diagnostics));
    return eve::editing::applied<EnvironmentAssetCard>(std::move(result));
}

Result<EnvironmentAssetPage> EnvironmentAssetBrowser::query(std::string text, std::size_t offset, std::size_t limit,
                                                            std::optional<std::uint64_t> generation) const {
    if (!database_ || limit == 0)
        return eve::editing::failed<EnvironmentAssetPage>(
            EditorStatus::Rejected, RuleId("editor.environment.invalid-browser-query"),
            "Environment browser requires AssetDB and positive page size");
    AssetQuery filter;
    filter.typeIds = {"cubemap", "image.cubemap", "environment-map"};
    filter.text    = std::move(text);
    auto page      = database_->query(filter, offset, limit, generation);
    if (!page.ok()) return Result<EnvironmentAssetPage>::failure(page.status());
    EnvironmentAssetPage result;
    result.nextOffset = page.value().nextOffset;
    result.hasMore    = page.value().hasMore;
    result.generation = page.value().generation;
    for (const auto& record : page.value().values) {
        auto value = card(record);
        if (value.ok()) result.values.push_back(value.value());
    }
    return eve::editing::applied<EnvironmentAssetPage>(std::move(result));
}

Result<EnvironmentAssetCard> EnvironmentAssetBrowser::select(const AssetGuid& asset) const {
    if (!database_ || asset.empty())
        return eve::editing::failed<EnvironmentAssetCard>(EditorStatus::Rejected,
                                                          RuleId("editor.environment.invalid-selection"),
                                                          "Environment selection requires AssetDB and asset id");
    auto found = database_->find(asset);
    if (!found.ok()) return Result<EnvironmentAssetCard>::failure(found.status());
    return card(found.value());
}

}  // namespace eve::editor

// EditorEventTimeline.cpp
namespace eve::editor {
namespace {

std::size_t nodeCount(const EditorValue& value, std::size_t limit) {
    std::size_t result = 1;
    if (const auto* array = value.getIf<EditorValue::Array>()) {
        for (const auto& child : *array) {
            if (result > limit) break;
            result += nodeCount(child, limit - std::min(result, limit));
        }
    } else if (const auto* object = value.getIf<EditorValue::Object>()) {
        for (const auto& [key, child] : *object) {
            (void)key;
            if (result > limit) break;
            result += nodeCount(child, limit - std::min(result, limit));
        }
    }
    return result;
}

bool includes(const std::vector<std::string>& values, const std::string& value) {
    return values.empty() || std::find(values.begin(), values.end(), value) != values.end();
}

int severityRank(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Info: return 0;
        case DiagnosticSeverity::Warning: return 1;
        case DiagnosticSeverity::Error: return 2;
        case DiagnosticSeverity::Fatal: return 3;
    }
    return 0;
}

}  // namespace

Result<void> EditorEventTimeline::setCapacity(std::size_t capacity) {
    if (capacity == 0 || capacity > 1000000)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.timeline.capacity"),
                                          "Timeline capacity must be between 1 and 1,000,000");
    capacity_ = capacity;
    while (events_.size() > capacity_) {
        events_.pop_front();
        ++dropped_;
    }
    ++generation_;
    return eve::editing::applied<void>();
}

Result<std::uint64_t> EditorEventTimeline::append(EditorTimelineEvent event, std::size_t maximumPayloadNodes) {
    if (event.domain.empty() || event.type.empty() || maximumPayloadNodes == 0 ||
        nodeCount(event.payload, maximumPayloadNodes) > maximumPayloadNodes)
        return eve::editing::failed<std::uint64_t>(EditorStatus::Rejected, RuleId("editor.timeline.invalid-event"),
                                                   "Timeline event identity or payload budget is invalid");
    if (event.sequence == 0)
        event.sequence = nextSequence_++;
    else {
        if (event.sequence < nextSequence_)
            return eve::editing::failed<std::uint64_t>(EditorStatus::Conflict, RuleId("editor.timeline.sequence"),
                                                       "Timeline event sequence is stale or duplicated");
        nextSequence_ = event.sequence + 1;
    }
    const std::uint64_t sequence = event.sequence;
    if (events_.size() == capacity_) {
        events_.pop_front();
        ++dropped_;
    }
    events_.push_back(std::move(event));
    ++generation_;
    return eve::editing::applied<std::uint64_t>(sequence);
}

Result<EditorTimelinePage> EditorEventTimeline::query(const EditorTimelineQuery& filter, std::size_t offset,
                                                      std::size_t                  limit,
                                                      std::optional<std::uint64_t> generation) const {
    if (limit == 0 || limit > 10000)
        return eve::editing::failed<EditorTimelinePage>(EditorStatus::Rejected, RuleId("editor.timeline.page-size"),
                                                        "Timeline page size must be between 1 and 10,000");
    if (generation && *generation != generation_)
        return eve::editing::failed<EditorTimelinePage>(
            EditorStatus::Conflict, RuleId("editor.timeline.stale-generation"), "Timeline changed while paging");
    std::vector<EditorTimelineEvent> matched;
    for (const auto& event : events_) {
        if (!includes(filter.domains, event.domain) || !includes(filter.types, event.type) ||
            (!filter.source.empty() && event.source != filter.source) ||
            (!filter.correlation.empty() && event.correlation != filter.correlation) ||
            (filter.minimumSeverity && severityRank(event.severity) < severityRank(*filter.minimumSeverity)) ||
            event.tick < filter.firstTick || (filter.lastTick > 0 && event.tick > filter.lastTick))
            continue;
        matched.push_back(event);
    }
    EditorTimelinePage result;
    result.generation       = generation_;
    result.droppedEvents    = dropped_;
    const std::size_t begin = std::min(offset, matched.size());
    const std::size_t end   = std::min(begin + limit, matched.size());
    result.values.assign(matched.begin() + static_cast<std::ptrdiff_t>(begin),
                         matched.begin() + static_cast<std::ptrdiff_t>(end));
    result.nextOffset = end;
    result.hasMore    = end < matched.size();
    return eve::editing::applied<EditorTimelinePage>(std::move(result));
}

void EditorEventTimeline::clear() {
    events_.clear();
    ++generation_;
}

}  // namespace eve::editor

// EditorExtension.cpp
namespace eve::editor {

Result<void> EditorExtensionRegistry::load(IGameEditorExtension& extension) {
    if (!commands_)
        return invalid("editor.extension.missing-command-service", "Extension registry has no command service");
    const std::string owner = extension.ownerModule();
    if (owner.empty()) return invalid("editor.extension.missing-owner", "Game editor extension owner is required");
    if (std::any_of(commandVisibility_.begin(), commandVisibility_.end(),
                    [&](const CommandVisibility& command) { return command.ownerModule == owner; }) ||
        std::any_of(tools_.begin(), tools_.end(),
                    [&](const ExtensionToolDescriptor& tool) { return tool.ownerModule == owner; }))
        return invalid("editor.extension.duplicate-owner", "Game editor extension owner is already loaded");
    activeOwner_ = owner;
    try {
        extension.registerEditor(*this);
    } catch (const std::exception& exception) {
        unload(owner);
        activeOwner_.clear();
        return invalid("editor.extension.exception", exception.what());
    } catch (...) {
        unload(owner);
        activeOwner_.clear();
        return invalid("editor.extension.exception", "Game editor extension threw an unknown exception");
    }
    activeOwner_.clear();
    return eve::editing::applied<void>();
}

std::size_t EditorExtensionRegistry::unload(const std::string& ownerModule) {
    std::size_t removed    = commands_ ? commands_->unregisterOwner(ownerModule) : 0;
    auto        eraseOwner = [&](auto& values) {
        const std::size_t before = values.size();
        std::erase_if(values, [&](const auto& value) { return value.ownerModule == ownerModule; });
        removed += before - values.size();
    };
    eraseOwner(commandVisibility_);
    eraseOwner(tools_);
    eraseOwner(palettes_);
    eraseOwner(rules_);
    return removed;
}

void EditorExtensionRegistry::configureProfile(HostProfile& profile) const {
    const ExtensionAudience audience = audienceFor(profile);
    for (const CommandVisibility& command : commandVisibility_)
        if (hasAudience(command.audiences, audience)) profile.allowCommand(command.id);
}

std::vector<ExtensionToolDescriptor> EditorExtensionRegistry::tools(const HostProfile& profile) const {
    std::vector<ExtensionToolDescriptor> result;
    const ExtensionAudience              audience = audienceFor(profile);
    for (const ExtensionToolDescriptor& tool : tools_)
        if (hasAudience(tool.audiences, audience)) result.push_back(tool);
    return result;
}

std::vector<ExtensionPaletteDescriptor> EditorExtensionRegistry::palettes(const HostProfile& profile) const {
    std::vector<ExtensionPaletteDescriptor> result;
    const ExtensionAudience                 audience = audienceFor(profile);
    for (const ExtensionPaletteDescriptor& palette : palettes_)
        if (hasAudience(palette.audiences, audience)) result.push_back(palette);
    return result;
}

std::vector<ExtensionRuleDescriptor> EditorExtensionRegistry::rules(const HostProfile& profile) const {
    std::vector<ExtensionRuleDescriptor> result;
    const ExtensionAudience              audience = audienceFor(profile);
    for (const ExtensionRuleDescriptor& rule : rules_)
        if (hasAudience(rule.audiences, audience)) result.push_back(rule);
    return result;
}

Result<EditorValue> EditorExtensionRegistry::registerCommand(CommandDescriptor descriptor, EditorCommandHandler handler,
                                                             ExtensionAudience audiences) {
    if (activeOwner_.empty())
        return eve::editing::failed<EditorValue>(EditorStatus::Rejected,
                                                 RuleId("editor.extension.registration-outside-load"),
                                                 "Commands may only be registered while loading an extension");
    descriptor.ownerModule     = activeOwner_;
    Result<EditorValue> result = commands_->registerCommand(descriptor, std::move(handler));
    if (result.ok()) commandVisibility_.push_back({descriptor.id, activeOwner_, audiences});
    return result;
}

Result<void> EditorExtensionRegistry::registerTool(ExtensionToolDescriptor descriptor) {
    if (activeOwner_.empty() || descriptor.id.empty())
        return invalid("editor.extension.invalid-tool", "Tool registration requires an active owner and id");
    if (std::any_of(tools_.begin(), tools_.end(),
                    [&](const ExtensionToolDescriptor& tool) { return tool.id == descriptor.id; }))
        return invalid("editor.extension.duplicate-tool", "Tool id is already registered");
    descriptor.ownerModule = activeOwner_;
    tools_.push_back(std::move(descriptor));
    return eve::editing::applied<void>();
}

Result<void> EditorExtensionRegistry::registerPalette(ExtensionPaletteDescriptor descriptor) {
    if (activeOwner_.empty() || descriptor.id.empty())
        return invalid("editor.extension.invalid-palette", "Palette registration requires an active owner and id");
    descriptor.ownerModule = activeOwner_;
    palettes_.push_back(std::move(descriptor));
    return eve::editing::applied<void>();
}

Result<void> EditorExtensionRegistry::registerRule(ExtensionRuleDescriptor descriptor) {
    if (activeOwner_.empty() || descriptor.id.empty())
        return invalid("editor.extension.invalid-rule", "Rule registration requires an active owner and id");
    descriptor.ownerModule = activeOwner_;
    rules_.push_back(std::move(descriptor));
    return eve::editing::applied<void>();
}

ExtensionAudience EditorExtensionRegistry::audienceFor(const HostProfile& profile) {
    switch (profile.kind()) {
        case HostKind::Developer: return ExtensionAudience::Developer;
        case HostKind::RuntimeBuilder: return ExtensionAudience::Player;
        case HostKind::RuntimeAdmin: return ExtensionAudience::Admin;
        case HostKind::Automation: return ExtensionAudience::Automation;
    }
    return ExtensionAudience::None;
}

Result<void> EditorExtensionRegistry::invalid(const char* rule, std::string message) {
    return eve::editing::failed<void>(EditorStatus::Rejected, RuleId(rule), std::move(message));
}

}  // namespace eve::editor
