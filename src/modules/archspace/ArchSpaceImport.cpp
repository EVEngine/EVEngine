#include "archspace/ArchSpaceImport.h"

#include "archspace/ArchSpaceDocument.h"

#include <cmath>
#include <queue>
#include <utility>
#include <vector>

namespace eve::archspace {
namespace {

constexpr double kEps = 1e-8;

/** Floor-only flood fill so Portal cells stay bridges between regions. */
bool isFloor(ImportCellKind kind) { return kind == ImportCellKind::Floor; }

}  // namespace

eve::Result<void> importCellGrid(Document& document, const std::string& levelId, const CellGridImport& grid,
                                 const std::string& idPrefix) {
    const Node* level = document.find(levelId);
    if (!level || level->kind != NodeKind::Level)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                 "ArchSpace import requires an existing level",
                                                                 "levelId", {}, "archspace.import"));
    if (idPrefix.empty() || idPrefix.size() > 64)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace import idPrefix is invalid", "idPrefix", {},
                                                                 "archspace.import"));
    if (grid.width <= 0 || grid.height <= 0 || grid.width > 4096 || grid.height > 4096)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace import grid size is out of range", "size",
                                                                 {}, "archspace.import"));
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= kEps || !std::isfinite(grid.originX) ||
        !std::isfinite(grid.originZ))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace import cellSize/origin must be finite",
                                                                 "cellSize", {}, "archspace.import"));
    const std::size_t expected = static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
    if (grid.cells.size() != expected)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace import cells size must equal width*height",
                                                                 "cells", {}, "archspace.import"));

    Document candidate = document;
    if (candidate.topology() == TopologyKind::Architectural) {
        auto topology = candidate.setTopology(TopologyKind::Dungeon);
        if (!topology.ok()) return topology;
    }

    const int                W = grid.width;
    const int                H = grid.height;
    std::vector<int>         labels(expected, -1);
    int                      regionCount = 0;
    std::vector<std::string> regionIds;

    auto indexOf = [W](int x, int z) { return z * W + x; };

    for (int z = 0; z < H; ++z) {
        for (int x = 0; x < W; ++x) {
            const int idx = indexOf(x, z);
            if (!isFloor(grid.cells[static_cast<std::size_t>(idx)]) || labels[static_cast<std::size_t>(idx)] >= 0)
                continue;
            const int                       label = regionCount++;
            int                             minX = x, maxX = x, minZ = z, maxZ = z;
            std::queue<std::pair<int, int>> q;
            q.push({x, z});
            labels[static_cast<std::size_t>(idx)] = label;
            while (!q.empty()) {
                const auto [cx, cz] = q.front();
                q.pop();
                minX                        = std::min(minX, cx);
                maxX                        = std::max(maxX, cx);
                minZ                        = std::min(minZ, cz);
                maxZ                        = std::max(maxZ, cz);
                static constexpr int kDx[4] = {1, -1, 0, 0};
                static constexpr int kDz[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    const int nx = cx + kDx[d];
                    const int nz = cz + kDz[d];
                    if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                    const int nidx = indexOf(nx, nz);
                    if (labels[static_cast<std::size_t>(nidx)] >= 0) continue;
                    if (!isFloor(grid.cells[static_cast<std::size_t>(nidx)])) continue;
                    labels[static_cast<std::size_t>(nidx)] = label;
                    q.push({nx, nz});
                }
            }
            const std::string regionId = idPrefix + ".region." + std::to_string(label);
            regionIds.push_back(regionId);
            const double x0 = grid.originX + static_cast<double>(minX) * grid.cellSize;
            const double z0 = grid.originZ + static_cast<double>(minZ) * grid.cellSize;
            const double x1 = grid.originX + static_cast<double>(maxX + 1) * grid.cellSize;
            const double z1 = grid.originZ + static_cast<double>(maxZ + 1) * grid.cellSize;
            auto         created =
                candidate.createRegion(levelId, regionId, regionId, {{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}});
            if (!created.ok()) return created;
        }
    }

    int portalCount = 0;
    for (int z = 0; z < H; ++z) {
        for (int x = 0; x < W; ++x) {
            const int idx = indexOf(x, z);
            if (grid.cells[static_cast<std::size_t>(idx)] != ImportCellKind::Portal) continue;
            int                  first  = -1;
            int                  second = -1;
            static constexpr int kDx[4] = {1, -1, 0, 0};
            static constexpr int kDz[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                const int nx = x + kDx[d];
                const int nz = z + kDz[d];
                if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                const int nlabel = labels[static_cast<std::size_t>(indexOf(nx, nz))];
                if (nlabel < 0) continue;
                if (first < 0) {
                    first = nlabel;
                } else if (nlabel != first && second < 0) {
                    second = nlabel;
                }
            }
            if (first < 0 || second < 0) continue;
            const int         a        = std::min(first, second);
            const int         b        = std::max(first, second);
            const std::string portalId = idPrefix + ".portal." + std::to_string(a) + "." + std::to_string(b) + "." +
                                         std::to_string(portalCount++);
            const Vec3 position{grid.originX + (static_cast<double>(x) + 0.5) * grid.cellSize, 0.0,
                                grid.originZ + (static_cast<double>(z) + 0.5) * grid.cellSize};
            auto created = candidate.createPortal(levelId, portalId, portalId, regionIds[static_cast<std::size_t>(a)],
                                                  regionIds[static_cast<std::size_t>(b)], kPassageWalk, position);
            if (!created.ok()) return created;
        }
    }

    document = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::archspace
