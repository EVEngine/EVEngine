#include "archspace/editor/EditorArchSpaceTools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace eve::editor {
namespace {

constexpr double kEps = 1e-8;

bool intersectFloor(const ArchSpaceViewportRay& ray, double elevation, archspace::Vec2& out) {
    if (std::abs(ray.direction[1]) <= kEps) return false;
    const double t = (elevation - ray.origin[1]) / ray.direction[1];
    if (t < 0.0) return false;
    out.x = ray.origin[0] + ray.direction[0] * t;
    out.z = ray.origin[2] + ray.direction[2] * t;
    return std::isfinite(out.x) && std::isfinite(out.z);
}

archspace::Vec2 snapOrtho(const archspace::Vec2& start, const archspace::Vec2& end) {
    if (std::abs(end.x - start.x) >= std::abs(end.z - start.z)) return {end.x, start.z};
    return {start.x, end.z};
}

bool projectOntoWall(const archspace::Node& wall, const archspace::Vec2& point, double& t, double& dist) {
    const double dx   = wall.end.x - wall.start.x;
    const double dz   = wall.end.z - wall.start.z;
    const double len2 = dx * dx + dz * dz;
    if (len2 <= kEps) return false;
    t               = std::clamp(((point.x - wall.start.x) * dx + (point.z - wall.start.z) * dz) / len2, 0.0, 1.0);
    const double px = wall.start.x + dx * t;
    const double pz = wall.start.z + dz * t;
    dist            = std::sqrt((point.x - px) * (point.x - px) + (point.z - pz) * (point.z - pz));
    return true;
}

bool commitDomain(editing::IEditAuthority* authority, eve::archspace_editing::ArchSpaceDocumentTarget& target,
                  editing::DomainOperation operation, const ToolDescriptor& descriptor, std::uint64_t sequence,
                  std::string mergeKey, std::optional<editing::TransactionReceipt>& outReceipt) {
    if (!authority) return false;
    editing::TransactionSpec transaction;
    transaction.id           = editing::TransactionId(descriptor.id + "." + std::to_string(sequence));
    transaction.label        = descriptor.label;
    transaction.origin       = editing::ActionOrigin::User;
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    transaction.mergeKey     = std::move(mergeKey);
    const std::array<editing::DomainOperation, 1> operations{std::move(operation)};
    auto plan = authority->preflight(transaction, std::span<const editing::DomainOperation>(operations));
    if (!plan.ok()) return false;
    auto receipt = authority->commit(plan.value());
    if (!receipt.ok()) return false;
    outReceipt = std::move(receipt.value());
    return true;
}

}  // namespace

ArchSpaceWallDrawTool::ArchSpaceWallDrawTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority)
    : viewport_(viewport), authority_(authority) {}

void ArchSpaceWallDrawTool::setViewportAdapter(IArchSpaceViewportAdapter* viewport) {
    viewport_ = viewport;
    drawing_  = false;
}

void ArchSpaceWallDrawTool::setAuthority(editing::IEditAuthority* authority) {
    authority_ = authority;
    drawing_   = false;
}

void ArchSpaceWallDrawTool::setLevelId(std::string levelId) { levelId_ = std::move(levelId); }

bool ArchSpaceWallDrawTool::hitFloor(const EditorPointerEvent& event, archspace::Vec2& out) const {
    if (!viewport_) return false;
    auto ray = viewport_->pointerRay(event);
    if (!ray.ok()) return false;
    return intersectFloor(ray.value(), 0.0, out);
}

ToolResponse ArchSpaceWallDrawTool::begin(EditorContext&, const EditorPointerEvent& event) {
    if (levelId_.empty() || !viewport_) return ToolResponse::ignored();
    archspace::Vec2 point;
    if (!hitFloor(event, point)) return ToolResponse::ignored();
    if (!(contiguous_ && haveAnchor_ && !lastWallId_.empty())) {
        anchor_     = point;
        haveAnchor_ = true;
    }
    cursor_  = point;
    drawing_ = true;
    return ToolResponse::capture();
}

ToolResponse ArchSpaceWallDrawTool::move(const EditorPointerEvent& event) {
    if (!drawing_) return ToolResponse::ignored();
    archspace::Vec2 point;
    if (!hitFloor(event, point)) return ToolResponse::consumed();
    cursor_ = event.shift ? snapOrtho(anchor_, point) : point;
    return ToolResponse::consumed();
}

bool ArchSpaceWallDrawTool::commitWall(eve::archspace_editing::ArchSpaceDocumentTarget& target, archspace::Vec2 start,
                                       archspace::Vec2 end) {
    const double dx = end.x - start.x;
    const double dz = end.z - start.z;
    if (std::sqrt(dx * dx + dz * dz) <= 0.05) return false;
    const std::string wallId = idPrefix_ + "." + std::to_string(++sequence_);
    auto operation           = target.makeCreateWall(levelId_, wallId, wallId, start, end, wallHeight_, wallThickness_);
    if (!operation.ok()) return false;
    if (!commitDomain(authority_, target, std::move(operation.value()), descriptor_, sequence_, "archspace.wall.draw",
                      lastReceipt_))
        return false;
    lastWallId_ = wallId;
    if (contiguous_) {
        anchor_     = end;
        haveAnchor_ = true;
    }
    return true;
}

ToolResponse ArchSpaceWallDrawTool::finish(EditorContext& context, const EditorPointerEvent& event) {
    if (!drawing_) return ToolResponse::ignored();
    (void)move(event);
    drawing_     = false;
    auto* target = dynamic_cast<eve::archspace_editing::ArchSpaceDocumentTarget*>(context.target());
    if (!target || !authority_) return ToolResponse::release();
    (void)commitWall(*target, anchor_, cursor_);
    return ToolResponse::release();
}

ToolResponse ArchSpaceWallDrawTool::pointerEvent(EditorContext& context, const EditorPointerEvent& event) {
    if (event.phase == EditorPointerEvent::Phase::Down)
        return event.button == 0 ? begin(context, event) : ToolResponse::ignored();
    if (event.phase == EditorPointerEvent::Phase::Move) return move(event);
    if (event.phase == EditorPointerEvent::Phase::Up) return finish(context, event);
    if (event.phase == EditorPointerEvent::Phase::Cancel) {
        cancel(context);
        return ToolResponse::release();
    }
    return ToolResponse::ignored();
}

void ArchSpaceWallDrawTool::deactivate(EditorContext& context) { cancel(context); }
void ArchSpaceWallDrawTool::cancel(EditorContext&) { drawing_ = false; }

void ArchSpaceWallDrawTool::drawOverlay(EditorContext&, IEditorOverlay& overlay) {
    if (!drawing_ || !viewport_) return;
    auto from = viewport_->projectWorld({anchor_.x, 0.05, anchor_.z});
    auto to   = viewport_->projectWorld({cursor_.x, 0.05, cursor_.z});
    if (!from.ok() || !to.ok()) return;
    OverlayStyle style;
    style.color     = 0xff4fc3f7U;
    style.thickness = 2.f;
    overlay.line(from.value(), to.value(), style);
    overlay.circle(from.value(), 5.f, style);
    overlay.circle(to.value(), 5.f, style);
}

void ArchSpaceWallDrawTool::inspect(EditorContext&, IEditorInspector& inspector) {
    inspector.beginGroup("archspace.wall", "Wall Draw");
    float height    = static_cast<float>(wallHeight_);
    float thickness = static_cast<float>(wallThickness_);
    if (inspector.scalar("height", "Height", height, 0.5f, 10.f)) wallHeight_ = height;
    if (inspector.scalar("thickness", "Thickness", thickness, 0.05f, 1.0f)) wallThickness_ = thickness;
    inspector.boolean("contiguous", "Contiguous", contiguous_);
    inspector.string("levelId", "Level Id", levelId_);
    inspector.endGroup();
}

ArchSpaceOpeningPlaceTool::ArchSpaceOpeningPlaceTool(IArchSpaceViewportAdapter* viewport,
                                                     editing::IEditAuthority*   authority)
    : viewport_(viewport), authority_(authority) {}

void ArchSpaceOpeningPlaceTool::setViewportAdapter(IArchSpaceViewportAdapter* viewport) {
    viewport_ = viewport;
    preview_.reset();
}
void ArchSpaceOpeningPlaceTool::setAuthority(editing::IEditAuthority* authority) {
    authority_ = authority;
    preview_.reset();
}

bool ArchSpaceOpeningPlaceTool::hitFloor(const EditorPointerEvent& event, archspace::Vec2& out) const {
    if (!viewport_) return false;
    auto ray = viewport_->pointerRay(event);
    if (!ray.ok()) return false;
    return intersectFloor(ray.value(), 0.0, out);
}

std::optional<ArchSpaceOpeningPlaceTool::Pick> ArchSpaceOpeningPlaceTool::pickWall(
    const eve::archspace_editing::ArchSpaceDocumentTarget& target, const archspace::Vec2& point) const {
    std::optional<Pick> best;
    double              bestDist = 1e9;
    for (const auto& [id, node] : target.document().nodes()) {
        if (node.kind != archspace::NodeKind::Wall) continue;
        double t    = 0.5;
        double dist = 0.0;
        if (!projectOntoWall(node, point, t, dist)) continue;
        if (dist > std::max(0.75, node.thickness * 2.0)) continue;
        if (dist >= bestDist) continue;
        bestDist = dist;
        best     = Pick{
            id, t, {node.start.x + (node.end.x - node.start.x) * t, node.start.z + (node.end.z - node.start.z) * t}};
    }
    return best;
}

ToolResponse ArchSpaceOpeningPlaceTool::pointerEvent(EditorContext& context, const EditorPointerEvent& event) {
    auto* target = dynamic_cast<eve::archspace_editing::ArchSpaceDocumentTarget*>(context.target());
    if (!target || !viewport_) return ToolResponse::ignored();
    archspace::Vec2 point;
    if (!hitFloor(event, point)) {
        preview_.reset();
        return event.phase == EditorPointerEvent::Phase::Move ? ToolResponse::consumed() : ToolResponse::ignored();
    }
    preview_ = pickWall(*target, point);
    if (event.phase == EditorPointerEvent::Phase::Move) return ToolResponse::consumed();
    if (event.phase == EditorPointerEvent::Phase::Down && event.button == 0 && preview_) return ToolResponse::capture();
    if (event.phase == EditorPointerEvent::Phase::Cancel) {
        preview_.reset();
        return ToolResponse::release();
    }
    if (event.phase != EditorPointerEvent::Phase::Up || event.button != 0) return ToolResponse::ignored();
    if (!preview_ || !authority_) return ToolResponse::release();
    const std::string openingId = idPrefix_ + "." + std::to_string(++sequence_);
    auto operation = target->makeCreateOpening(preview_->wallId, openingId, kind_, preview_->t, width_, height_, sill_);
    if (!operation.ok()) return ToolResponse::release();
    if (commitDomain(authority_, *target, std::move(operation.value()), descriptor_, sequence_,
                     "archspace.opening.place", lastReceipt_))
        lastOpeningId_ = openingId;
    preview_.reset();
    return ToolResponse::release();
}

void ArchSpaceOpeningPlaceTool::deactivate(EditorContext& context) { cancel(context); }
void ArchSpaceOpeningPlaceTool::cancel(EditorContext&) { preview_.reset(); }

void ArchSpaceOpeningPlaceTool::drawOverlay(EditorContext&, IEditorOverlay& overlay) {
    if (!preview_ || !viewport_) return;
    auto center = viewport_->projectWorld({preview_->point.x, height_ * 0.5, preview_->point.z});
    if (!center.ok()) return;
    OverlayStyle style;
    style.color     = kind_ == archspace::OpeningKind::Door ? 0xffffb74dU : 0xff81d4faU;
    style.thickness = 2.f;
    overlay.circle(center.value(), 8.f, style);
    overlay.text(center.value(), kind_ == archspace::OpeningKind::Door ? "door" : "window", style);
}

void ArchSpaceOpeningPlaceTool::inspect(EditorContext&, IEditorInspector& inspector) {
    inspector.beginGroup("archspace.opening", "Opening Place");
    bool isDoor = kind_ == archspace::OpeningKind::Door;
    if (inspector.boolean("door", "Door (else window)", isDoor))
        kind_ = isDoor ? archspace::OpeningKind::Door : archspace::OpeningKind::Window;
    float width  = static_cast<float>(width_);
    float height = static_cast<float>(height_);
    float sill   = static_cast<float>(sill_);
    if (inspector.scalar("width", "Width", width, 0.3f, 3.0f)) width_ = width;
    if (inspector.scalar("height", "Height", height, 0.3f, 4.0f)) height_ = height;
    if (inspector.scalar("sill", "Sill", sill, 0.0f, 2.5f)) sill_ = sill;
    inspector.endGroup();
}

ArchSpaceItemPlaceTool::ArchSpaceItemPlaceTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority)
    : viewport_(viewport), authority_(authority) {}

void ArchSpaceItemPlaceTool::setViewportAdapter(IArchSpaceViewportAdapter* viewport) {
    viewport_ = viewport;
    preview_.reset();
}
void ArchSpaceItemPlaceTool::setAuthority(editing::IEditAuthority* authority) {
    authority_ = authority;
    preview_.reset();
}
void ArchSpaceItemPlaceTool::setLevelId(std::string levelId) { levelId_ = std::move(levelId); }
void ArchSpaceItemPlaceTool::setCatalogId(std::string catalogId) { catalogId_ = std::move(catalogId); }

bool ArchSpaceItemPlaceTool::hitFloor(const EditorPointerEvent& event, archspace::Vec2& out) const {
    if (!viewport_) return false;
    auto ray = viewport_->pointerRay(event);
    if (!ray.ok()) return false;
    return intersectFloor(ray.value(), 0.0, out);
}

ToolResponse ArchSpaceItemPlaceTool::pointerEvent(EditorContext& context, const EditorPointerEvent& event) {
    auto* target = dynamic_cast<eve::archspace_editing::ArchSpaceDocumentTarget*>(context.target());
    if (!target || levelId_.empty() || !viewport_) return ToolResponse::ignored();
    archspace::Vec2 point;
    if (!hitFloor(event, point)) {
        preview_.reset();
        return event.phase == EditorPointerEvent::Phase::Move ? ToolResponse::consumed() : ToolResponse::ignored();
    }
    preview_ = point;
    if (event.phase == EditorPointerEvent::Phase::Move) return ToolResponse::consumed();
    if (event.phase == EditorPointerEvent::Phase::Down && event.button == 0) return ToolResponse::capture();
    if (event.phase == EditorPointerEvent::Phase::Cancel) {
        preview_.reset();
        return ToolResponse::release();
    }
    if (event.phase != EditorPointerEvent::Phase::Up || event.button != 0) return ToolResponse::ignored();
    if (!authority_) return ToolResponse::release();
    const std::string itemId = idPrefix_ + "." + std::to_string(++sequence_);
    auto              operation =
        target->makePlaceItem(levelId_, itemId, catalogId_, archspace::Vec3{point.x, 0.0, point.z}, yawDegrees_);
    if (!operation.ok()) return ToolResponse::release();
    if (commitDomain(authority_, *target, std::move(operation.value()), descriptor_, sequence_, "archspace.item.place",
                     lastReceipt_))
        lastItemId_ = itemId;
    preview_.reset();
    return ToolResponse::release();
}

void ArchSpaceItemPlaceTool::deactivate(EditorContext& context) { cancel(context); }
void ArchSpaceItemPlaceTool::cancel(EditorContext&) { preview_.reset(); }

void ArchSpaceItemPlaceTool::drawOverlay(EditorContext&, IEditorOverlay& overlay) {
    if (!preview_ || !viewport_) return;
    auto center = viewport_->projectWorld({preview_->x, 0.05, preview_->z});
    if (!center.ok()) return;
    OverlayStyle style;
    style.color     = 0xffaed581U;
    style.thickness = 2.f;
    overlay.circle(center.value(), 7.f, style);
    overlay.text(center.value(), catalogId_, style);
}

void ArchSpaceItemPlaceTool::inspect(EditorContext&, IEditorInspector& inspector) {
    inspector.beginGroup("archspace.item", "Item Place");
    inspector.string("levelId", "Level Id", levelId_);
    inspector.string("catalogId", "Catalog Id", catalogId_);
    float yaw = static_cast<float>(yawDegrees_);
    if (inspector.scalar("yaw", "Yaw Degrees", yaw, -180.f, 180.f)) yawDegrees_ = yaw;
    inspector.endGroup();
}

}  // namespace eve::editor
