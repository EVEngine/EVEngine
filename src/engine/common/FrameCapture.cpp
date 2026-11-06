#include "common/FrameCapture.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>

namespace eve {
namespace {
constexpr std::array<std::string_view, 11> names{
    "final",          "gbuffer.depth",     "gbuffer.hwDepth",  "gbuffer.normal",
    "gbuffer.albedo", "gbuffer.pbrParams", "gbuffer.emissive", "gbuffer.velocity",
    "shadow.csm.0",   "shadow.csm.1",      "shadow.csm.2"};
std::atomic<uint64_t> nextGeneration{1};
template <class T>
Result<T> failure(std::string message, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "frame_capture"));
}
bool terminal(FrameCapturePhase p) {
    return p == FrameCapturePhase::Failed || p == FrameCapturePhase::Cancelled || p == FrameCapturePhase::Expired ||
           p == FrameCapturePhase::Released;
}
}  // namespace

std::string_view frameAttachmentName(FrameAttachment a) {
    const auto i = static_cast<size_t>(a);
    return i < names.size() ? names[i] : "unknown";
}
Result<FrameAttachment> parseFrameAttachment(std::string_view name) {
    auto it = std::find(names.begin(), names.end(), name);
    if (it == names.end()) return failure<FrameAttachment>("UnknownAttachment: " + std::string(name));
    return Result<FrameAttachment>::success(static_cast<FrameAttachment>(it - names.begin()));
}
std::string_view frameCapturePhaseName(FrameCapturePhase p) {
    constexpr std::array<std::string_view, 8> phases{"queued", "recording", "readbackPending", "ready",
                                                     "failed", "cancelled", "expired",         "released"};
    auto                                      i = static_cast<size_t>(p);
    return i < phases.size() ? phases[i] : "unknown";
}

struct FrameCaptureStore::Impl {
    struct Entry {
        FrameCaptureRequest                   request;
        FrameCaptureSnapshot                  snapshot;
        std::chrono::steady_clock::time_point deadline;
        size_t                                reserved = 0;
        size_t                                cpuBytes = 0;
    };
    Clock                                                                     clock;
    FrameCaptureCapabilities                                                  caps;
    uint64_t                                                                  nextToken = 1;
    std::map<std::string, Entry, std::less<>>                                 entries;
    std::vector<std::pair<std::weak_ptr<const std::vector<uint8_t>>, size_t>> leases;
    size_t                                                                    used() const {
        size_t bytes = 0;
        for (const auto& [key, e] : entries) {
            (void)key;
            bytes += e.reserved;
        }
        for (const auto& [lease, size] : leases)
            if (!lease.expired()) bytes += size;
        return bytes;
    }
};

FrameCaptureStore::FrameCaptureStore() : FrameCaptureStore(std::chrono::steady_clock::now) {}
FrameCaptureStore::FrameCaptureStore(Clock clock) : impl_(std::make_unique<Impl>()) {
    impl_->clock           = std::move(clock);
    impl_->caps.generation = nextGeneration.fetch_add(1);
}
FrameCaptureStore::~FrameCaptureStore() = default;
void FrameCaptureStore::configure(FrameCaptureCapabilities caps) {
    caps.generation = impl_->caps.generation;
    impl_->caps     = std::move(caps);
}
FrameCaptureCapabilities FrameCaptureStore::capabilities() const { return impl_->caps; }
void                     FrameCaptureStore::collect() {
    std::erase_if(impl_->leases, [](const auto& item) { return item.first.expired(); });
    const auto now = impl_->clock();
    for (auto& [key, e] : impl_->entries) {
        (void)key;
        if (!terminal(e.snapshot.phase) && now >= e.deadline) {
            e.snapshot.phase  = FrameCapturePhase::Expired;
            e.snapshot.reason = "CaptureExpired";
            e.snapshot.buffers.clear();
            e.cpuBytes = 0;
        }
    }
    // Bound diagnostic tombstones without invalidating live captures or GPU reservations.
    while (impl_->entries.size() > 64) {
        auto it = std::find_if(impl_->entries.begin(), impl_->entries.end(), [](const auto& item) {
            return terminal(item.second.snapshot.phase) && item.second.reserved == 0;
        });
        if (it == impl_->entries.end()) break;
        impl_->entries.erase(it);
    }
}
Result<std::string> FrameCaptureStore::request(FrameCaptureRequest request) {
    collect();
    if (request.attachments.empty() || request.attachments.size() > names.size() || request.timeout.count() < 100 ||
        request.timeout.count() > 30000 || request.requestKey.size() > 128)
        return failure<std::string>("InvalidCaptureRequest");
    std::set<FrameAttachment> unique;
    for (auto a : request.attachments) {
        if (frameAttachmentName(a) == "unknown" || !unique.insert(a).second)
            return failure<std::string>("InvalidOrDuplicateAttachment");
        if (std::find(impl_->caps.attachments.begin(), impl_->caps.attachments.end(), a) ==
            impl_->caps.attachments.end())
            return failure<std::string>("UnsupportedAttachment: " + std::string(frameAttachmentName(a)),
                                        DiagnosticCode::Unsupported);
    }
    size_t live = 0;
    for (const auto& [token, e] : impl_->entries) {
        if (e.snapshot.generation == impl_->caps.generation && !request.requestKey.empty() &&
            request.requestKey == e.request.requestKey) {
            if (request.attachments != e.request.attachments || request.timeout != e.request.timeout ||
                request.reportUnavailable != e.request.reportUnavailable)
                return failure<std::string>("RequestConflict");
            return Result<std::string>::success(token);
        }
        if (!terminal(e.snapshot.phase)) {
            ++live;
            if (e.snapshot.phase != FrameCapturePhase::Ready) return failure<std::string>("CaptureBusy");
        }
    }
    // Retain a bounded sequence for inspection sheets; GPU acquisition remains single-flight.
    if (live >= 16 || impl_->used() >= impl_->caps.maxBytes) return failure<std::string>("BudgetExceeded");
    Impl::Entry e;
    e.request             = std::move(request);
    e.snapshot.token      = std::to_string(impl_->caps.generation) + ":" + std::to_string(impl_->nextToken++);
    e.snapshot.generation = impl_->caps.generation;
    e.snapshot.backend    = impl_->caps.backend;
    e.deadline            = impl_->clock() + e.request.timeout;
    auto token            = e.snapshot.token;
    impl_->entries.emplace(token, std::move(e));
    return Result<std::string>::success(std::move(token));
}
Result<FrameCaptureSnapshot> FrameCaptureStore::snapshot(std::string_view token) {
    collect();
    auto it = impl_->entries.find(token);
    if (it == impl_->entries.end() || it->second.snapshot.generation != impl_->caps.generation)
        return failure<FrameCaptureSnapshot>("StaleCapture", DiagnosticCode::NotFound);
    return Result<FrameCaptureSnapshot>::success(it->second.snapshot);
}
Result<FrameCapturePhase> FrameCaptureStore::release(std::string_view token) {
    collect();
    auto it = impl_->entries.find(token);
    if (it == impl_->entries.end() || it->second.snapshot.generation != impl_->caps.generation)
        return failure<FrameCapturePhase>("StaleCapture", DiagnosticCode::NotFound);
    auto& e = it->second;
    if (e.snapshot.phase == FrameCapturePhase::Released)
        return Result<FrameCapturePhase>::success(FrameCapturePhase::Released);
    e.snapshot.phase =
        e.snapshot.phase == FrameCapturePhase::Ready ? FrameCapturePhase::Released : FrameCapturePhase::Cancelled;
    e.snapshot.buffers.clear();
    e.cpuBytes = 0;
    return Result<FrameCapturePhase>::success(e.snapshot.phase);
}
void FrameCaptureStore::invalidate() {
    for (auto& [key, entry] : impl_->entries) {
        (void)key;
        entry.snapshot.phase = FrameCapturePhase::Cancelled;
        entry.snapshot.buffers.clear();
        entry.cpuBytes = 0;
    }
    impl_->caps.generation = nextGeneration.fetch_add(1);
}
std::optional<std::pair<std::string, FrameCaptureRequest>> FrameCaptureStore::queued() {
    collect();
    for (const auto& [token, e] : impl_->entries)
        if (e.snapshot.phase == FrameCapturePhase::Queued) return std::make_pair(token, e.request);
    return std::nullopt;
}
Result<void> FrameCaptureStore::begin(std::string_view token, uint64_t frameId, size_t bytes) {
    collect();
    auto it = impl_->entries.find(token);
    if (it == impl_->entries.end() || it->second.snapshot.phase != FrameCapturePhase::Queued)
        return failure<void>("InvalidCaptureTransition");
    if (bytes > impl_->caps.maxBytes / 2 || impl_->used() > impl_->caps.maxBytes - bytes * 2)
        return failure<void>("BudgetExceeded");
    auto& e            = it->second;
    e.reserved         = bytes * 2;  // Reserve staging and peak CPU publication together.
    e.snapshot.frameId = frameId;
    e.snapshot.phase   = FrameCapturePhase::Recording;
    return Result<void>::success();
}
Result<void> FrameCaptureStore::submitted(std::string_view token) {
    auto it = impl_->entries.find(token);
    if (it == impl_->entries.end() || it->second.snapshot.phase != FrameCapturePhase::Recording)
        return failure<void>("InvalidCaptureTransition");
    it->second.snapshot.phase = FrameCapturePhase::ReadbackPending;
    return Result<void>::success();
}
Result<void> FrameCaptureStore::complete(std::string_view token, FrameCaptureSnapshot snapshot) {
    collect();
    auto it = impl_->entries.find(token);
    if (it == impl_->entries.end() || it->second.snapshot.phase != FrameCapturePhase::ReadbackPending)
        return failure<void>("InvalidCaptureTransition");
    auto&                     e = it->second;
    std::set<FrameAttachment> seen;
    std::set<const void*>     storage;
    size_t                    bytes = 0;
    for (const auto& b : snapshot.buffers) {
        if (!seen.insert(b.attachment).second) return failure<void>("DuplicateCaptureBuffer");
        if (!b.bytes) {
            if (!e.request.reportUnavailable || b.unavailableReason.empty())
                return failure<void>("AttachmentUnavailable");
            continue;
        }
        if (!b.width || !b.height || uint64_t(b.width) * b.height * 4 != b.bytes->size())
            return failure<void>("InvalidCaptureStorage");
        if (storage.insert(b.bytes.get()).second) bytes += b.bytes->size();
    }
    if (seen != std::set<FrameAttachment>(e.request.attachments.begin(), e.request.attachments.end()) ||
        bytes > e.reserved / 2)
        return failure<void>("IncompleteCapture");
    snapshot.token      = e.snapshot.token;
    snapshot.generation = e.snapshot.generation;
    snapshot.backend    = e.snapshot.backend;
    snapshot.frameId    = e.snapshot.frameId;
    snapshot.phase      = FrameCapturePhase::Ready;
    storage.clear();
    for (const auto& b : snapshot.buffers)
        if (b.bytes && storage.insert(b.bytes.get()).second) impl_->leases.emplace_back(b.bytes, b.bytes->size());
    e.snapshot = std::move(snapshot);
    e.cpuBytes = bytes;
    e.reserved -= bytes;  // Remaining reservation is retired with GPU staging.
    e.deadline = impl_->clock() + std::chrono::seconds(60);
    return Result<void>::success();
}
Result<void> FrameCaptureStore::fail(std::string_view token, std::string reason) {
    auto it = impl_->entries.find(token);
    if (it == impl_->entries.end()) return failure<void>("StaleCapture", DiagnosticCode::NotFound);
    if (!terminal(it->second.snapshot.phase)) {
        it->second.snapshot.phase  = FrameCapturePhase::Failed;
        it->second.snapshot.reason = std::move(reason);
        it->second.snapshot.buffers.clear();
        it->second.cpuBytes = 0;
    }
    return Result<void>::success();
}
void FrameCaptureStore::retire(std::string_view token) {
    auto it = impl_->entries.find(token);
    if (it != impl_->entries.end()) it->second.reserved = 0;
}
}  // namespace eve
