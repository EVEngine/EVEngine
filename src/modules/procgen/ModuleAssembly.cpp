#include "procgen/ModuleAssembly.h"

#include "common/Value.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace eve::procgen {
namespace {

Result<void> reject(DiagnosticCode code, std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.moduleAssembly"));
}

struct Voxel {
    int x;
    int z;
    int level;
};

int normalizedTurn(int turn) { return ((turn % 4) + 4) % 4; }

Voxel rotateCell(int x, int z, int level, int width, int depth, int turn) {
    switch (normalizedTurn(turn)) {
        case 1: return {depth - 1 - z, x, level};
        case 2: return {width - 1 - x, depth - 1 - z, level};
        case 3: return {z, width - 1 - x, level};
        default: return {x, z, level};
    }
}

ModuleFacing rotateFacing(ModuleFacing facing, int turn) {
    if (facing == ModuleFacing::Up || facing == ModuleFacing::Down) return facing;
    return static_cast<ModuleFacing>((static_cast<int>(facing) + normalizedTurn(turn)) % 4);
}

Voxel facingStep(ModuleFacing facing) {
    switch (facing) {
        case ModuleFacing::North: return {0, -1, 0};
        case ModuleFacing::East: return {1, 0, 0};
        case ModuleFacing::South: return {0, 1, 0};
        case ModuleFacing::West: return {-1, 0, 0};
        case ModuleFacing::Up: return {0, 0, 1};
        case ModuleFacing::Down: return {0, 0, -1};
    }
    return {};
}

ModuleFacing opposite(ModuleFacing facing) {
    if (facing == ModuleFacing::Up) return ModuleFacing::Down;
    if (facing == ModuleFacing::Down) return ModuleFacing::Up;
    return static_cast<ModuleFacing>((static_cast<int>(facing) + 2) % 4);
}

const ModuleDefinition* findDefinition(const std::vector<ModuleDefinition>& definitions, const std::string& id) {
    const auto it =
        std::find_if(definitions.begin(), definitions.end(), [&](const auto& item) { return item.id == id; });
    return it == definitions.end() ? nullptr : &*it;
}

std::vector<Voxel> occupiedVoxels(const ModuleDefinition& definition, const ModulePlacement& placement) {
    std::vector<Voxel> result;
    result.reserve(static_cast<size_t>(definition.widthCells) * definition.depthCells * definition.heightLevels);
    for (int level = 0; level < definition.heightLevels; ++level)
        for (int z = 0; z < definition.depthCells; ++z)
            for (int x = 0; x < definition.widthCells; ++x) {
                const Voxel local =
                    rotateCell(x, z, level, definition.widthCells, definition.depthCells, placement.quarterTurn);
                result.push_back({placement.cellX + local.x, placement.cellZ + local.z, placement.level + local.level});
            }
    return result;
}

struct WorldConnector {
    int                x;
    int                z;
    int                level;
    ModuleFacing       facing;
    const std::string* tag;
    const std::string* accepts;
};

std::vector<WorldConnector> worldConnectors(const ModuleDefinition& definition, const ModulePlacement& placement) {
    std::vector<WorldConnector> result;
    result.reserve(definition.connectors.size());
    for (const auto& connector : definition.connectors) {
        const Voxel local = rotateCell(connector.cellX, connector.cellZ, connector.levelOffset, definition.widthCells,
                                       definition.depthCells, placement.quarterTurn);
        result.push_back({placement.cellX + local.x, placement.cellZ + local.z, placement.level + local.level,
                          rotateFacing(connector.facing, placement.quarterTurn), &connector.tag, &connector.accepts});
    }
    return result;
}

bool compatible(const WorldConnector& a, const WorldConnector& b) {
    return *a.accepts == *b.tag && *b.accepts == *a.tag;
}

bool exact(const Value::Object& object, std::initializer_list<const char*> fields) {
    std::set<std::string> expected;
    for (const char* field : fields) expected.emplace(field);
    if (object.size() != expected.size()) return false;
    for (const auto& [key, value] : object) {
        (void)value;
        if (!expected.contains(key)) return false;
    }
    return true;
}

const double* number(const Value& value) { return value.getIf<double>(); }

bool integer(const Value& value, int& output) {
    const auto* encoded = value.getIf<int64_t>();
    if (!encoded || *encoded < std::numeric_limits<int>::min() || *encoded > std::numeric_limits<int>::max())
        return false;
    output = static_cast<int>(*encoded);
    return true;
}

int facingFromName(const std::string& name) {
    static constexpr std::array<const char*, 6> names{"north", "east", "south", "west", "up", "down"};
    const auto                                  found = std::find(names.begin(), names.end(), name);
    return found == names.end() ? -1 : static_cast<int>(found - names.begin());
}

}  // namespace

ModuleAssemblyPlan::ModuleAssemblyPlan(ModuleAssemblyConstraints constraints) : constraints_(std::move(constraints)) {}

Result<void> ModuleAssemblyPlan::configure(float cellSize, float floorHeight, int minCellX, int maxCellX, int minCellZ,
                                           int maxCellZ, int maxLevels, bool bounded, bool requireConnection) {
    if (!definitions_.empty() || !placements_.empty())
        return reject(DiagnosticCode::PreconditionViolation,
                      "assembly constraints must be configured before definitions or placements", "constraints");
    if (!std::isfinite(cellSize) || cellSize <= 0.f || !std::isfinite(floorHeight) || floorHeight <= 0.f ||
        maxLevels <= 0 || (bounded && (minCellX > maxCellX || minCellZ > maxCellZ)))
        return reject(DiagnosticCode::InvalidArgument, "assembly dimensions and bounds must be valid", "constraints");
    constraints_.cellSize          = cellSize;
    constraints_.floorHeight       = floorHeight;
    constraints_.minCellX          = minCellX;
    constraints_.maxCellX          = maxCellX;
    constraints_.minCellZ          = minCellZ;
    constraints_.maxCellZ          = maxCellZ;
    constraints_.maxLevels         = maxLevels;
    constraints_.bounded           = bounded;
    constraints_.requireConnection = requireConnection;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ModuleAssemblyPlan::setAllowedQuarterTurns(bool turn0, bool turn90, bool turn180, bool turn270) {
    if (!definitions_.empty() || !placements_.empty())
        return reject(DiagnosticCode::PreconditionViolation,
                      "allowed rotations must be configured before definitions or placements", "constraints");
    if (!turn0 && !turn90 && !turn180 && !turn270)
        return reject(DiagnosticCode::InvalidArgument, "at least one module rotation must be allowed", "constraints");
    constraints_.allowedQuarterTurns = {turn0, turn90, turn180, turn270};
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ModuleAssemblyPlan::setMinimumSupportRatio(float ratio) {
    if (!definitions_.empty() || !placements_.empty())
        return reject(DiagnosticCode::PreconditionViolation,
                      "minimum support must be configured before definitions or placements", "constraints");
    if (!std::isfinite(ratio) || ratio < 0.f || ratio > 1.f)
        return reject(DiagnosticCode::InvalidArgument, "minimum support ratio must be between zero and one",
                      "constraints.minimumSupportRatio");
    constraints_.minimumSupportRatio = ratio;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ModuleAssemblyPlan::applyConfigJson(const std::string& json) {
    if (!definitions_.empty() || !placements_.empty())
        return reject(DiagnosticCode::PreconditionViolation, "JSON configuration requires an empty plan", "config");
    if (json.size() > 1024U * 1024U)
        return reject(DiagnosticCode::InvalidArgument, "assembly configuration exceeds 1 MiB", "config");
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<void>::failure(parsed.status());
    const auto* root = parsed.value().getIf<Value::Object>();
    if (!root || !exact(*root, {"schema", "version", "unknownFields", "constraints", "modules"}))
        return reject(DiagnosticCode::InvalidArgument, "assembly configuration root fields are invalid", "config");
    const auto *schema      = root->at("schema").getIf<std::string>(),
               *unknown     = root->at("unknownFields").getIf<std::string>();
    const auto* version     = root->at("version").getIf<int64_t>();
    const auto* constraints = root->at("constraints").getIf<Value::Object>();
    const auto* modules     = root->at("modules").getIf<Value::Array>();
    if (!schema || *schema != "eve.procgen.module-assembly" || !version || *version != 1 || !unknown ||
        *unknown != "reject" || !constraints || !modules || modules->empty() || modules->size() > 4096 ||
        !exact(*constraints, {"cellSize", "floorHeight", "bounds", "maxLevels", "allowedQuarterTurns",
                              "requireConnection", "minimumSupportRatio"}))
        return reject(DiagnosticCode::InvalidArgument, "assembly configuration schema is invalid", "config");

    const auto *cellSize = number(constraints->at("cellSize")), *floorHeight = number(constraints->at("floorHeight")),
               *support           = number(constraints->at("minimumSupportRatio"));
    const auto* bounds            = constraints->at("bounds").getIf<Value::Object>();
    const auto* turns             = constraints->at("allowedQuarterTurns").getIf<Value::Array>();
    const auto* requireConnection = constraints->at("requireConnection").getIf<bool>();
    int         maxLevels         = 0;
    if (!cellSize || !floorHeight || !support || !bounds || !turns || turns->size() != 4 || !requireConnection ||
        !integer(constraints->at("maxLevels"), maxLevels) ||
        !exact(*bounds, {"enabled", "minCellX", "maxCellX", "minCellZ", "maxCellZ"}))
        return reject(DiagnosticCode::InvalidArgument, "assembly constraint fields are invalid", "constraints");
    const auto*         bounded = bounds->at("enabled").getIf<bool>();
    int                 minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    std::array<bool, 4> allowed{};
    for (size_t i = 0; i < allowed.size(); ++i) {
        const auto* enabled = (*turns)[i].getIf<bool>();
        if (!enabled)
            return reject(DiagnosticCode::InvalidArgument, "rotation entries must be booleans", "constraints");
        allowed[i] = *enabled;
    }
    if (!bounded || !integer(bounds->at("minCellX"), minX) || !integer(bounds->at("maxCellX"), maxX) ||
        !integer(bounds->at("minCellZ"), minZ) || !integer(bounds->at("maxCellZ"), maxZ))
        return reject(DiagnosticCode::InvalidArgument, "assembly bounds are invalid", "constraints.bounds");

    ModuleAssemblyPlan candidate;
    auto configured = candidate.configure(float(*cellSize), float(*floorHeight), minX, maxX, minZ, maxZ, maxLevels,
                                          *bounded, *requireConnection);
    if (!configured) return configured;
    auto rotations = candidate.setAllowedQuarterTurns(allowed[0], allowed[1], allowed[2], allowed[3]);
    if (!rotations) return rotations;
    auto supported = candidate.setMinimumSupportRatio(float(*support));
    if (!supported) return supported;
    for (const Value& encodedModule : *modules) {
        const auto* module = encodedModule.getIf<Value::Object>();
        if (!module || !exact(*module, {"id", "widthCells", "depthCells", "heightLevels", "connectors"}))
            return reject(DiagnosticCode::InvalidArgument, "module fields are invalid", "modules");
        const auto* id         = module->at("id").getIf<std::string>();
        const auto* connectors = module->at("connectors").getIf<Value::Array>();
        int         width = 0, depth = 0, height = 0;
        if (!id || !connectors || connectors->size() > 16384 || !integer(module->at("widthCells"), width) ||
            !integer(module->at("depthCells"), depth) || !integer(module->at("heightLevels"), height))
            return reject(DiagnosticCode::InvalidArgument, "module values are invalid", "modules");
        auto registered = candidate.registerVolumeModuleType(*id, width, depth, height);
        if (!registered) return registered;
        for (const Value& encodedConnector : *connectors) {
            const auto* connector = encodedConnector.getIf<Value::Object>();
            if (!connector || !exact(*connector, {"cellX", "cellZ", "level", "facing", "tag", "accepts"}))
                return reject(DiagnosticCode::InvalidArgument, "connector fields are invalid", *id);
            int         x = 0, z = 0, level = 0;
            const auto *facing  = connector->at("facing").getIf<std::string>(),
                       *tag     = connector->at("tag").getIf<std::string>(),
                       *accepts = connector->at("accepts").getIf<std::string>();
            if (!facing || !tag || !accepts || !integer(connector->at("cellX"), x) ||
                !integer(connector->at("cellZ"), z) || !integer(connector->at("level"), level))
                return reject(DiagnosticCode::InvalidArgument, "connector values are invalid", *id);
            auto added = candidate.addVolumeConnector(*id, x, z, level, facingFromName(*facing), *tag, *accepts);
            if (!added) return added;
        }
    }
    *this = std::move(candidate);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ModuleAssemblyPlan::registerModuleType(std::string id, int widthCells, int depthCells) {
    return registerModule({std::move(id), widthCells, depthCells, {}});
}

Result<void> ModuleAssemblyPlan::registerVolumeModuleType(std::string id, int widthCells, int depthCells,
                                                          int heightLevels) {
    ModuleDefinition definition{std::move(id), widthCells, depthCells, {}};
    definition.heightLevels = heightLevels;
    return registerModule(std::move(definition));
}

Result<void> ModuleAssemblyPlan::addConnector(const std::string& moduleId, int cellX, int cellZ, int facing,
                                              std::string tag, std::string accepts) {
    return addVolumeConnector(moduleId, cellX, cellZ, 0, facing, std::move(tag), std::move(accepts));
}

Result<void> ModuleAssemblyPlan::addVolumeConnector(const std::string& moduleId, int cellX, int cellZ, int levelOffset,
                                                    int facing, std::string tag, std::string accepts) {
    auto definition =
        std::find_if(definitions_.begin(), definitions_.end(), [&](const auto& item) { return item.id == moduleId; });
    if (definition == definitions_.end())
        return reject(DiagnosticCode::NotFound, "module definition is not registered", moduleId);
    if (std::any_of(placements_.begin(), placements_.end(),
                    [&](const auto& placement) { return placement.moduleId == moduleId; }))
        return reject(DiagnosticCode::PreconditionViolation,
                      "connectors cannot change after the module has been placed", moduleId);
    if (facing < 0 || facing > 5 || cellX < 0 || cellX >= definition->widthCells || cellZ < 0 ||
        cellZ >= definition->depthCells || levelOffset < 0 || levelOffset >= definition->heightLevels || tag.empty() ||
        accepts.empty())
        return reject(DiagnosticCode::InvalidArgument, "connector fields are outside the module contract", moduleId);
    definition->connectors.push_back(
        {cellX, cellZ, static_cast<ModuleFacing>(facing), std::move(tag), std::move(accepts), levelOffset});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ModuleAssemblyPlan::registerModule(ModuleDefinition definition) {
    if (definition.id.empty()) return reject(DiagnosticCode::InvalidArgument, "module id must not be empty", "id");
    if (definition.widthCells <= 0 || definition.depthCells <= 0 || definition.heightLevels <= 0)
        return reject(DiagnosticCode::InvalidArgument, "module volume dimensions must be positive", definition.id);
    if (findDefinition(definitions_, definition.id))
        return reject(DiagnosticCode::AlreadyExists, "module id is already registered", definition.id);
    for (const auto& connector : definition.connectors) {
        if (connector.cellX < 0 || connector.cellX >= definition.widthCells || connector.cellZ < 0 ||
            connector.cellZ >= definition.depthCells || connector.levelOffset < 0 ||
            connector.levelOffset >= definition.heightLevels || connector.tag.empty() || connector.accepts.empty())
            return reject(DiagnosticCode::InvalidArgument, "connector must reference a volume cell and typed tags",
                          definition.id);
    }
    definitions_.push_back(std::move(definition));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ModuleAssemblyPlan::place(ModulePlacement placement) {
    const ModuleDefinition* definition = findDefinition(definitions_, placement.moduleId);
    if (!definition) return reject(DiagnosticCode::NotFound, "module definition is not registered", placement.moduleId);
    placement.quarterTurn = normalizedTurn(placement.quarterTurn);
    if (!constraints_.allowedQuarterTurns[static_cast<size_t>(placement.quarterTurn)])
        return reject(DiagnosticCode::PreconditionViolation, "module rotation is disabled by the assembly constraints",
                      placement.moduleId);
    if (placement.level < 0 || int64_t(placement.level) + definition->heightLevels > constraints_.maxLevels)
        return reject(DiagnosticCode::PreconditionViolation, "module level is outside the configured range",
                      placement.moduleId);

    const int width = placement.quarterTurn % 2 ? definition->depthCells : definition->widthCells;
    const int depth = placement.quarterTurn % 2 ? definition->widthCells : definition->depthCells;
    if (int64_t(placement.cellX) + width - 1 > std::numeric_limits<int>::max() ||
        int64_t(placement.cellZ) + depth - 1 > std::numeric_limits<int>::max())
        return reject(DiagnosticCode::InvalidArgument, "module coordinates overflow the grid range",
                      placement.moduleId);

    const auto                  newVoxels = occupiedVoxels(*definition, placement);
    std::vector<WorldConnector> existingConnectors;
    std::vector<Voxel>          existingVoxels;
    bool                        matchedConnection = false;
    for (const auto& existing : placements_) {
        const auto* existingDefinition = findDefinition(definitions_, existing.moduleId);
        const auto  occupied           = occupiedVoxels(*existingDefinition, existing);
        {
            for (const Voxel& candidate : newVoxels)
                if (std::any_of(occupied.begin(), occupied.end(), [&](Voxel cell) {
                        return cell.x == candidate.x && cell.z == candidate.z && cell.level == candidate.level;
                    }))
                    return reject(DiagnosticCode::Conflict, "module volume overlaps an existing placement",
                                  placement.moduleId);
            auto connectors = worldConnectors(*existingDefinition, existing);
            existingConnectors.insert(existingConnectors.end(), connectors.begin(), connectors.end());
            existingVoxels.insert(existingVoxels.end(), occupied.begin(), occupied.end());
        }
    }
    if (constraints_.bounded)
        for (const Voxel& cell : newVoxels)
            if (cell.x < constraints_.minCellX || cell.x > constraints_.maxCellX || cell.z < constraints_.minCellZ ||
                cell.z > constraints_.maxCellZ)
                return reject(DiagnosticCode::PreconditionViolation, "module footprint crosses assembly bounds",
                              placement.moduleId);

    if (placement.level > 0 && constraints_.minimumSupportRatio > 0.f) {
        const size_t bottomCellCount    = size_t(definition->widthCells) * definition->depthCells;
        size_t       supportedCellCount = 0;
        for (const Voxel& cell : newVoxels) {
            if (cell.level != placement.level) continue;
            if (std::any_of(existingVoxels.begin(), existingVoxels.end(), [&](Voxel other) {
                    return other.x == cell.x && other.z == cell.z && other.level == cell.level - 1;
                }))
                ++supportedCellCount;
        }
        const size_t requiredSupport =
            static_cast<size_t>(std::ceil(double(constraints_.minimumSupportRatio) * double(bottomCellCount)));
        if (supportedCellCount < requiredSupport)
            return reject(DiagnosticCode::PreconditionViolation,
                          "elevated module does not meet the configured vertical support ratio", placement.moduleId);
    }

    const auto connectors = worldConnectors(*definition, placement);
    for (const Voxel& cell : newVoxels) {
        for (int direction = 0; direction < 6; ++direction) {
            const auto    facing           = static_cast<ModuleFacing>(direction);
            const Voxel   step             = facingStep(facing);
            const int64_t neighborX        = int64_t(cell.x) + step.x;
            const int64_t neighborZ        = int64_t(cell.z) + step.z;
            const int64_t neighborLevel    = int64_t(cell.level) + step.level;
            bool          neighborOccupied = false;
            neighborOccupied = std::any_of(existingVoxels.begin(), existingVoxels.end(), [&](Voxel other) {
                return other.x == neighborX && other.z == neighborZ && other.level == neighborLevel;
            });
            if (!neighborOccupied) continue;
            const auto own = std::find_if(connectors.begin(), connectors.end(), [&](const auto& connector) {
                return connector.x == cell.x && connector.z == cell.z && connector.level == cell.level &&
                       connector.facing == facing;
            });
            const auto other =
                std::find_if(existingConnectors.begin(), existingConnectors.end(), [&](const auto& connector) {
                    return connector.x == neighborX && connector.z == neighborZ && connector.level == neighborLevel &&
                           connector.facing == opposite(facing);
                });
            if (own == connectors.end() || other == existingConnectors.end() || !compatible(*own, *other))
                return reject(DiagnosticCode::Conflict, "touching module edges do not expose compatible connectors",
                              placement.moduleId);
            matchedConnection = true;
        }
    }
    if (constraints_.requireConnection && !placements_.empty() && !matchedConnection)
        return reject(DiagnosticCode::PreconditionViolation, "module is disconnected from the existing assembly",
                      placement.moduleId);
    placements_.push_back(std::move(placement));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::string ModuleAssemblyPlan::getPlacementModule(int index) const {
    return index >= 0 && static_cast<size_t>(index) < placements_.size()
               ? placements_[static_cast<size_t>(index)].moduleId
               : std::string{};
}

float ModuleAssemblyPlan::getPlacementX(int index) const noexcept {
    return index >= 0 && static_cast<size_t>(index) < placements_.size()
               ? float(placements_[static_cast<size_t>(index)].cellX) * constraints_.cellSize
               : 0.f;
}

float ModuleAssemblyPlan::getPlacementY(int index) const noexcept {
    return index >= 0 && static_cast<size_t>(index) < placements_.size()
               ? float(placements_[static_cast<size_t>(index)].level) * constraints_.floorHeight
               : 0.f;
}

float ModuleAssemblyPlan::getPlacementZ(int index) const noexcept {
    return index >= 0 && static_cast<size_t>(index) < placements_.size()
               ? float(placements_[static_cast<size_t>(index)].cellZ) * constraints_.cellSize
               : 0.f;
}

float ModuleAssemblyPlan::getPlacementYawDegrees(int index) const noexcept {
    return index >= 0 && static_cast<size_t>(index) < placements_.size()
               ? float(normalizedTurn(placements_[static_cast<size_t>(index)].quarterTurn)) * 90.f
               : 0.f;
}

}  // namespace eve::procgen
