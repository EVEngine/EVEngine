#include "procgen/house/HouseGeneration.h"

#include "procgen/house/HouseGenerationState.h"

namespace eve::housegen {
namespace {
Result<void> invalidStage(std::string message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "housegen.stages"));
}
}  // namespace

HouseGeneration::HouseGeneration()                                      = default;
HouseGeneration::~HouseGeneration()                                     = default;
HouseGeneration::HouseGeneration(HouseGeneration&&) noexcept            = default;
HouseGeneration& HouseGeneration::operator=(HouseGeneration&&) noexcept = default;

Result<void> house_detail::State::plan() {
    if (planned) return Result<void>::success();
    if (!isPolygon && z > 0 && inset == 0 && r.width > 4 && r.depth > 4 && (rng() & 3u) == 0u) inset = 1;
    auto mask = makeMask(inset);
    if (!previousMask.empty()) {
        for (size_t i = 0; i < mask.size(); ++i) mask[i] = uint8_t(mask[i] && previousMask[i]);
    }
    procgen::Grid2D grid;
    grid.resize(r.width, r.depth);
    for (int y = 0; y < r.depth; ++y)
        for (int x = 0; x < r.width; ++x) grid.setCell(x, y, mask[size_t(y * r.width + x)]);
    planned = std::move(grid);
    return Result<void>::success();
}

Result<void> HouseGeneration::configure(const HouseRequest& request, const HouseComponentLibrary& library) {
    auto candidate      = std::make_unique<house_detail::State>();
    candidate->r        = request;
    candidate->library_ = library;
    auto result         = candidate->configure();
    if (!result.ok()) return result;
    state_ = std::move(candidate);
    return Result<void>::success();
}

int HouseGeneration::floorCount() const noexcept { return state_ ? state_->z : 0; }

Result<procgen::Grid2D> HouseGeneration::nextFloorGrid() {
    if (!state_ || state_->z >= state_->r.floors)
        return Result<procgen::Grid2D>::failure(invalidStage("no next house floor").status());
    auto result = state_->plan();
    if (!result.ok()) return Result<procgen::Grid2D>::failure(result.status());
    return Result<procgen::Grid2D>::success(*state_->planned);
}

Result<void> HouseGeneration::emitFloor(const procgen::Grid2D& grid, const std::vector<std::string>& rooms) {
    if (!state_ || !state_->planned || state_->z >= state_->r.floors)
        return invalidStage("prepare nextFloorGrid before emitting a floor");
    const auto& r = state_->r;
    if (grid.getWidth() != r.width || grid.getHeight() != r.depth || rooms.empty() ||
        std::any_of(rooms.begin(), rooms.end(), [](const auto& name) { return name.empty(); }))
        return invalidStage("floor grid dimensions and nonempty room types must match the request");
    if (std::none_of(grid.cells().begin(), grid.cells().end(), [](auto cell) { return cell != 0; }))
        return invalidStage("house floor must contain occupied cells");
    if (!state_->previousMask.empty()) {
        for (size_t i = 0; i < grid.cells().size(); ++i)
            if (grid.cells()[i] && !state_->previousMask[i]) return invalidStage("upper floor lacks direct support");
    }
    if (rooms.size() > 1 && (state_->library_.byCategory("interior_wall", r.style).empty() ||
                             state_->library_.byCategory("interior_door", r.style).empty()))
        return invalidStage("multi-room floor requires interior wall and door components");
    auto candidate = std::make_unique<house_detail::State>(*state_);
    if (candidate->z == 0) {
        std::transform(grid.cells().begin(), grid.cells().end(), candidate->baseMask.begin(),
                       [](auto cell) { return uint8_t(cell != 0); });
        if (candidate->stairwell) {
            candidate->stairwell = house_detail::findInteriorCell(candidate->baseMask, r.width, r.depth);
            if (!candidate->stairwell) return invalidStage("edited floor has no interior stair cell");
        }
    } else if (candidate->stairwell) {
        auto [x, y] = *candidate->stairwell;
        if (!grid.getCell(x, y)) return invalidStage("upper floor removes the stair column");
    }
    auto result = candidate->floor(grid, rooms);
    if (!result.ok()) return result;
    state_ = std::move(candidate);
    return Result<void>::success();
}

Result<HouseLayout> HouseGeneration::finish() const {
    if (!state_ || state_->z != state_->r.floors)
        return Result<HouseLayout>::failure(invalidStage("emit all requested floors before finishing").status());
    auto candidate = std::make_unique<house_detail::State>(*state_);
    auto result    = candidate->finish();
    if (!result.ok()) return Result<HouseLayout>::failure(result.status());
    return Result<HouseLayout>::success(std::move(candidate->generated));
}
}  // namespace eve::housegen
