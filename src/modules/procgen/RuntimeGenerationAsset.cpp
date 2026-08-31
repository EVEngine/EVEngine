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
    out << "EVPCG_CELL 2 " << worldSeed_ << ' ' << level << ' ' << x << ' ' << z << ' '
        << found->second.revision << ' ' << levels_[size_t(level)].cellSize << '\n';
    out << "POINTS " << found->second.output.getCount() << '\n';
    const auto& points = found->second.output.points();
    for (size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        out << "POINT " << point.x << ' ' << point.y << ' ' << point.z << ' ' << point.normalX
            << ' ' << point.normalY << ' ' << point.normalZ << ' ' << point.pitch << ' '
            << point.yaw << ' ' << point.roll << ' ' << point.scaleX << ' ' << point.scaleY << ' '
            << point.scaleZ << ' ' << point.density << ' ' << point.seed << ' '
            << point.boundsMinX << ' ' << point.boundsMinY << ' ' << point.boundsMinZ << ' '
            << point.boundsMaxX << ' ' << point.boundsMaxY << ' ' << point.boundsMaxZ << ' '
            << point.colorR << ' ' << point.colorG << ' ' << point.colorB << ' ' << point.colorA
            << ' ' << point.steepness << '\n';
        std::vector<std::string> names;
        names.reserve(point.floatAttributes.size());
        for (const auto& [name, value] : point.floatAttributes) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
            out << "FLOAT " << index << ' ' << std::quoted(name) << ' '
                << point.floatAttributes.at(name) << '\n';
        names.clear();
        names.reserve(point.intAttributes.size());
        for (const auto& [name, value] : point.intAttributes) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
            out << "INT " << index << ' ' << std::quoted(name) << ' '
                << point.intAttributes.at(name) << '\n';
        names.clear();
        names.reserve(point.boolAttributes.size());
        for (const auto& [name, value] : point.boolAttributes) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
            out << "BOOL " << index << ' ' << std::quoted(name) << ' '
                << (point.boolAttributes.at(name) ? 1 : 0) << '\n';
        names.clear();
        names.reserve(point.vectorAttributes.size());
        for (const auto& [name, value] : point.vectorAttributes) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            const auto& value = point.vectorAttributes.at(name);
            out << "VECTOR " << index << ' ' << std::quoted(name) << ' ' << value.x << ' '
                << value.y << ' ' << value.z << '\n';
        }
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
        magic != "EVPCG_CELL" || (version != 1 && version != 2) || seed != worldSeed_ || revision == 0 ||
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
        if (!(input >> record) || record != "POINT")
            return false;
        if (version == 1) {
            if (!(input >> point.x >> point.y >> point.z >> point.normalX >> point.normalY >>
                  point.normalZ >> point.yaw >> point.scaleX >> point.scaleY >> point.scaleZ >>
                  point.density >> point.seed))
                return false;
        } else if (!(input >> point.x >> point.y >> point.z >> point.normalX >> point.normalY >>
                     point.normalZ >> point.pitch >> point.yaw >> point.roll >> point.scaleX >>
                     point.scaleY >> point.scaleZ >> point.density >> point.seed >> point.boundsMinX >>
                     point.boundsMinY >> point.boundsMinZ >> point.boundsMaxX >> point.boundsMaxY >>
                     point.boundsMaxZ >> point.colorR >> point.colorG >> point.colorB >> point.colorA >>
                     point.steepness)) {
            return false;
        }
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            !std::isfinite(point.normalX) || !std::isfinite(point.normalY) ||
            !std::isfinite(point.normalZ) || !std::isfinite(point.pitch) ||
            !std::isfinite(point.yaw) || !std::isfinite(point.roll) ||
            !std::isfinite(point.scaleX) || !std::isfinite(point.scaleY) ||
            !std::isfinite(point.scaleZ) || !std::isfinite(point.density) ||
            !std::isfinite(point.boundsMinX) || !std::isfinite(point.boundsMinY) ||
            !std::isfinite(point.boundsMinZ) || !std::isfinite(point.boundsMaxX) ||
            !std::isfinite(point.boundsMaxY) || !std::isfinite(point.boundsMaxZ) ||
            !std::isfinite(point.colorR) || !std::isfinite(point.colorG) ||
            !std::isfinite(point.colorB) || !std::isfinite(point.colorA) ||
            !std::isfinite(point.steepness))
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
            } else if (record == "INT") {
                int target = -1;
                std::string name;
                std::int64_t value = 0;
                if (!(input >> target >> std::quoted(name) >> value) || target != index || name.empty())
                    return false;
                restored.points().back().intAttributes[name] = value;
            } else if (record == "BOOL") {
                int target = -1;
                std::string name;
                int value = -1;
                if (!(input >> target >> std::quoted(name) >> value) || target != index || name.empty() ||
                    (value != 0 && value != 1))
                    return false;
                restored.points().back().boolAttributes[name] = value != 0;
            } else if (record == "VECTOR") {
                int target = -1;
                std::string name;
                ProcgenAttributeVector value;
                if (!(input >> target >> std::quoted(name) >> value.x >> value.y >> value.z) ||
                    target != index || name.empty() || !std::isfinite(value.x) ||
                    !std::isfinite(value.y) || !std::isfinite(value.z))
                    return false;
                restored.points().back().vectorAttributes[name] = value;
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
    const int restoredPoints = restored.getCount();
    const int replacedPoints =
        existing != cells_.end() ? existing->second.output.getCount() : 0;
    if ((maxPointsPerCell_ > 0 && restoredPoints > maxPointsPerCell_) ||
        (maxResidentPoints_ > 0 &&
         getResidentPointCount() - replacedPoints > maxResidentPoints_ - restoredPoints)) {
        ++rejectedOutputCount_;
        return false;
    }
    generateQueue_.erase(std::remove(generateQueue_.begin(), generateQueue_.end(), key),
                         generateQueue_.end());
    cleanupQueue_.erase(std::remove(cleanupQueue_.begin(), cleanupQueue_.end(), key),
                        cleanupQueue_.end());
    Cell& cell = cells_[key];
    cell.state = State::Active;
    cell.revision = revision;
    cell.ticket = ++nextTicket_;
    cell.failures = 0;
    cell.trimmed = false;
    cell.output = std::move(restored);
    return true;
}

}  // namespace eve::procgen
