#include "procgen/PointGraph.h"

#include <algorithm>
#include <iomanip>
#include <functional>
#include <sstream>

namespace eve::procgen {
namespace {

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        result.push_back(digits[byte >> 4u]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool hexDecode(const std::string& value, std::string& result) {
    if (value.size() % 2 != 0) return false;
    result.clear();
    result.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int high = hexDigit(value[i]);
        const int low  = hexDigit(value[i + 1]);
        if (high < 0 || low < 0) return false;
        result.push_back(char((high << 4) | low));
    }
    return true;
}

}  // namespace

std::string PointGraph::serializeDefinition() const {
    std::ostringstream out;
    out << "EVPCG_POINT_GRAPH 1\n";
    out << std::setprecision(9);
    for (const auto& id : nodeOrder_) {
        const auto& node = nodes_.at(id);
        out << "NODE " << std::quoted(id) << ' ' << std::quoted(node.operation) << '\n';
        std::vector<std::string> keys;
        keys.reserve(node.floats.size());
        for (const auto& [key, value] : node.floats) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys)
            out << "FLOAT " << std::quoted(id) << ' ' << std::quoted(key) << ' '
                << node.floats.at(key) << '\n';
        keys.clear();
        keys.reserve(node.ints.size());
        for (const auto& [key, value] : node.ints) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys)
            out << "INT " << std::quoted(id) << ' ' << std::quoted(key) << ' '
                << node.ints.at(key) << '\n';
        keys.clear();
        keys.reserve(node.strings.size());
        for (const auto& [key, value] : node.strings) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys)
            out << "STRING " << std::quoted(id) << ' ' << std::quoted(key) << ' '
                << std::quoted(node.strings.at(key)) << '\n';
        if (node.subgraph) {
            out << "SUBGRAPH " << std::quoted(id) << ' ' << std::quoted(node.subgraphInput) << ' '
                << std::quoted(node.subgraphOutput) << ' '
                << std::quoted(hexEncode(node.subgraph->serializeDefinition())) << '\n';
        }
    }
    std::vector<std::string> parameterNames = parameterOrder_;
    std::sort(parameterNames.begin(), parameterNames.end());
    for (const auto& name : parameterNames) {
        const auto& parameter = parameters_.at(name);
        out << "PARAM " << std::quoted(name) << ' ' << std::quoted(parameter.nodeId) << ' '
            << std::quoted(parameter.key) << '\n';
    }
    for (const auto& id : nodeOrder_) {
        const auto& node = nodes_.at(id);
        for (int input = 0; input < 2; ++input)
            if (!node.inputs[input].empty())
                out << "EDGE " << std::quoted(node.inputs[input]) << ' ' << std::quoted(id) << ' '
                    << input << '\n';
    }
    out << "END\n";
    return out.str();
}

bool PointGraph::deserializeDefinition(const std::string& definition) {
    PointGraph replacement;
    std::istringstream input(definition);
    std::string magic;
    int version = 0;
    if (!(input >> magic >> version) || magic != "EVPCG_POINT_GRAPH" || version != 1) {
        error_ = "invalid point graph header";
        return false;
    }
    std::string line;
    std::getline(input, line);
    bool ended = false;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream record(line);
        std::string kind;
        record >> kind;
        if (kind == "END") {
            ended = true;
            break;
        }
        std::string id;
        if (kind == "NODE") {
            std::string operation;
            if (!(record >> std::quoted(id) >> std::quoted(operation)) ||
                !replacement.addNode(id, operation)) {
                error_ = "invalid NODE record";
                return false;
            }
        } else if (kind == "EDGE") {
            std::string to;
            int slot = -1;
            if (!(record >> std::quoted(id) >> std::quoted(to) >> slot) ||
                !replacement.connect(id, to, slot)) {
                error_ = "invalid EDGE record";
                return false;
            }
        } else if (kind == "FLOAT") {
            std::string key;
            float value = 0.f;
            if (!(record >> std::quoted(id) >> std::quoted(key) >> value) ||
                !replacement.setNodeFloat(id, key, value)) {
                error_ = "invalid FLOAT record";
                return false;
            }
        } else if (kind == "INT") {
            std::string key;
            int value = 0;
            if (!(record >> std::quoted(id) >> std::quoted(key) >> value) ||
                !replacement.setNodeInt(id, key, value)) {
                error_ = "invalid INT record";
                return false;
            }
        } else if (kind == "STRING") {
            std::string key;
            std::string value;
            if (!(record >> std::quoted(id) >> std::quoted(key) >> std::quoted(value)) ||
                !replacement.setNodeString(id, key, value)) {
                error_ = "invalid STRING record";
                return false;
            }
        } else if (kind == "SUBGRAPH") {
            std::string nestedInput;
            std::string nestedOutput;
            std::string nestedEncoded;
            std::string nestedDefinition;
            if (!(record >> std::quoted(id) >> std::quoted(nestedInput) >>
                  std::quoted(nestedOutput) >> std::quoted(nestedEncoded)) ||
                !hexDecode(nestedEncoded, nestedDefinition)) {
                error_ = "invalid SUBGRAPH record";
                return false;
            }
            PointGraph nested;
            if (!nested.deserializeDefinition(nestedDefinition) ||
                !replacement.setNodeSubgraph(id, &nested, nestedInput, nestedOutput)) {
                error_ = "invalid nested graph at " + id + ": " + nested.getError();
                return false;
            }
        } else if (kind == "PARAM") {
            std::string nodeId;
            std::string key;
            if (!(record >> std::quoted(id) >> std::quoted(nodeId) >> std::quoted(key)) ||
                !replacement.exposeParameter(id, nodeId, key)) {
                error_ = "invalid PARAM record";
                return false;
            }
        } else {
            error_ = "unknown graph record: " + kind;
            return false;
        }
        record >> std::ws;
        if (!record.eof()) {
            error_ = "trailing graph record data: " + kind;
            return false;
        }
    }
    if (!ended) {
        error_ = "point graph END record is missing";
        return false;
    }

    // Structural cycle check without requiring runtime PointSet/SpatialData slots.
    std::unordered_map<std::string, int> states;
    std::function<bool(const std::string&)> visit = [&](const std::string& id) {
        if (states[id] == 2) return true;
        if (states[id] == 1) return false;
        states[id] = 1;
        const auto found = replacement.nodes_.find(id);
        if (found == replacement.nodes_.end()) return false;
        for (const auto& dependency : found->second.inputs)
            if (!dependency.empty() && !visit(dependency)) return false;
        states[id] = 2;
        return true;
    };
    for (const auto& id : replacement.nodeOrder_) {
        if (!visit(id)) {
            error_ = "cycle in serialized graph at node: " + id;
            return false;
        }
    }
    *this  = std::move(replacement);
    error_.clear();
    return true;
}

PointGraph* PointGraph::instantiate() const {
    auto* instance = new PointGraph();
    if (instance->deserializeDefinition(serializeDefinition())) return instance;
    delete instance;
    return nullptr;
}

}  // namespace eve::procgen
