#include "procgen/JsonExport.h"

#include "procgen/Semantic.h"

#include <fstream>
#include <sstream>

namespace eve::procgen {
namespace {

std::string escapeJson(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

}  // namespace

std::string gridToJson(const Grid2D &grid) {
    std::ostringstream oss;
    oss << "{\"width\":" << grid.getWidth() << ",\"height\":" << grid.getHeight()
        << ",\"algorithm\":\"" << escapeJson(grid.getMeta("algorithm", "")) << "\",\"cells\":[";
    const auto &cells = grid.cells();
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i) oss << ',';
        oss << cells[i];
    }
    oss << "],\"semantics\":[";
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i) oss << ',';
        oss << '"' << semanticName(cells[i]) << '"';
    }
    oss << "],\"objects\":[";
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        if (i) oss << ',';
        oss << "{\"name\":\"" << escapeJson(grid.getObjectName(i)) << "\",\"type\":\""
            << escapeJson(grid.getObjectType(i)) << "\",\"x\":" << grid.getObjectX(i)
            << ",\"y\":" << grid.getObjectY(i) << ",\"width\":" << grid.getObjectWidth(i)
            << ",\"height\":" << grid.getObjectHeight(i) << ",\"gid\":" << grid.getObjectGid(i)
            << '}';
    }
    oss << "]}";
    return oss.str();
}

bool writeGridJson(const Grid2D &grid, const std::string &path, std::string *error) {
    if (path.empty()) {
        if (error) *error = "json path is empty";
        return false;
    }
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        if (error) *error = "failed to open json path: " + path;
        return false;
    }
    ofs << gridToJson(grid);
    if (!ofs) {
        if (error) *error = "failed to write json path: " + path;
        return false;
    }
    return true;
}

}  // namespace eve::procgen
