#include "procgen/RuntimeGeneration.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::procgen {

std::string RuntimeGeneration::serializeCell(int level, int x, int z) const {
    const auto found = cells_.find({level, x, z});
    if (found == cells_.end() || found->second.state != State::Active) return {};
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "EVPCG_CELL 1 " << worldSeed_ << ' ' << level << ' ' << x << ' ' << z << ' '
        << found->second.revision << ' ' << levels_[size_t(level)].cellSize << '\n';
    out << "POINTS " << found->second.output.getCount() << '\n';
    const auto& points = found->second.output.points();
    for (size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        out << "POINT " << point.x << ' ' << point.y << ' ' << point.z << ' ' << point.normalX
            << ' ' << point.normalY << ' ' << point.normalZ << ' ' << point.yaw << ' '
            << point.scaleX << ' ' << point.scaleY << ' ' << point.scaleZ << ' ' << point.density
            << ' ' << point.seed << '\n';
        std::vector<std::string> names;
        names.reserve(point.floatAttributes.size());
        for (const auto& [name, value] : point.floatAttributes) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
            out << "FLOAT " << index << ' ' << std::quoted(name) << ' '
                << point.floatAttributes.at(name) << '\n';
        names.clear();
        names.reserve(point.stringAttributes.size());
        for (const auto& [name, value] : point.stringAttributes) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
            out << "STRING " << index << ' ' << std::quoted(name) << ' '
                << std::quoted(point.stringAttributes.at(name)) << '\n';
    }
    out << "END\n";
    return out.str();
}

bool RuntimeGeneration::deserializeCell(const std::string& definition) {
    constexpr int maxPersistedPoints = 10000000;
    std::istringstream input(definition);
    std::string magic;
    int version = 0;
    uint32_t seed = 0;
    CellKey key;
    uint64_t revision = 0;
    float cellSize = 0.f;
    if (!(input >> magic >> version >> seed >> key.level >> key.x >> key.z >> revision >>
          cellSize) ||
        magic != "EVPCG_CELL" || version != 1 || seed != worldSeed_ || revision == 0 ||
        key.level < 0 || key.level >= int(levels_.size()) || !std::isfinite(cellSize) ||
        cellSize != levels_[size_t(key.level)].cellSize)
        return false;
    std::string record;
    int pointCount = 0;
    if (!(input >> record >> pointCount) || record != "POINTS" || pointCount < 0 ||
        pointCount > maxPersistedPoints)
        return false;

    PointSet restored;
    for (int index = 0; index < pointCount; ++index) {
        ProcgenPoint point;
        if (!(input >> record) || record != "POINT" ||
            !(input >> point.x >> point.y >> point.z >> point.normalX >> point.normalY >>
              point.normalZ >> point.yaw >> point.scaleX >> point.scaleY >> point.scaleZ >>
              point.density >> point.seed))
            return false;
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            !std::isfinite(point.normalX) || !std::isfinite(point.normalY) ||
            !std::isfinite(point.normalZ) || !std::isfinite(point.yaw) ||
            !std::isfinite(point.scaleX) || !std::isfinite(point.scaleY) ||
            !std::isfinite(point.scaleZ) || !std::isfinite(point.density))
            return false;
        restored.points().push_back(std::move(point));
        while (true) {
            const std::streampos position = input.tellg();
            if (!(input >> record)) return false;
            if (record == "FLOAT") {
                int target = -1;
                std::string name;
                float value = 0.f;
                if (!(input >> target >> std::quoted(name) >> value) || target != index ||
                    name.empty() || !std::isfinite(value))
                    return false;
                restored.points().back().floatAttributes[name] = value;
            } else if (record == "STRING") {
                int target = -1;
                std::string name;
                std::string value;
                if (!(input >> target >> std::quoted(name) >> std::quoted(value)) ||
                    target != index || name.empty())
                    return false;
                restored.points().back().stringAttributes[name] = value;
            } else {
                input.seekg(position);
                break;
            }
        }
    }
    if (!(input >> record) || record != "END") return false;
    std::string trailing;
    if (input >> trailing) return false;

    const auto existing = cells_.find(key);
    const bool alreadyResident =
        existing != cells_.end() &&
        (existing->second.state == State::Active || existing->second.state == State::Generating);
    if (!alreadyResident && maxActiveCells_ > 0 &&
        getActiveCellCount() + getGeneratingCount() >= maxActiveCells_)
        return false;
    generateQueue_.erase(std::remove(generateQueue_.begin(), generateQueue_.end(), key),
                         generateQueue_.end());
    cleanupQueue_.erase(std::remove(cleanupQueue_.begin(), cleanupQueue_.end(), key),
                        cleanupQueue_.end());
    Cell& cell = cells_[key];
    cell.state = State::Active;
    cell.revision = revision;
    cell.ticket = ++nextTicket_;
    cell.failures = 0;
    cell.output = std::move(restored);
    return true;
}

}  // namespace eve::procgen
