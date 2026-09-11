#include "common/Exception.h"
#include "tensor/KernelGenWgslInternal.h"

#include <cctype>
#include <map>

namespace eve::tensor::wgsl_detail {
namespace {
int storageNode(const Graph &graph, int id) {
    const auto &node = graph.node(id);
    switch (node.type) {
        case OpType::Reshape:
        case OpType::Flatten:
        case OpType::Cast: return storageNode(graph, node.in0);
        default: return id;
    }
}

// Specialize only our generated declarations and identifier tokens, never arbitrary user shaders.
void specializeSource(std::string &source, const std::vector<int> &representatives, int inputCount) {
    std::vector<std::string>           names(static_cast<size_t>(inputCount));
    std::map<std::string, std::string> aliases;
    for (int i = 0; i < inputCount; ++i) {
        const std::string prefix = "@group(0) @binding(" + std::to_string(i) + ") var<storage, read_write> ";
        const auto        begin  = source.find(prefix);
        if (begin == std::string::npos) throw eve::Exception("Tensor WGSL: missing input declaration");
        const auto nameBegin          = begin + prefix.size();
        const auto nameEnd            = source.find(':', nameBegin);
        names[static_cast<size_t>(i)] = source.substr(nameBegin, nameEnd - nameBegin);
        if (representatives[static_cast<size_t>(i)] == i) continue;
        aliases.emplace(names[static_cast<size_t>(i)],
                        names[static_cast<size_t>(representatives[static_cast<size_t>(i)])]);
        source.erase(begin, source.find('\n', nameEnd) + 1 - begin);
    }
    if (aliases.empty()) return;
    std::string renamed;
    for (size_t i = 0; i < source.size();) {
        const unsigned char ch = static_cast<unsigned char>(source[i]);
        if (std::isalpha(ch) || ch == '_') {
            const auto begin = i++;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
            const std::string word  = source.substr(begin, i - begin);
            const auto        found = aliases.find(word);
            renamed += found == aliases.end() ? word : found->second;
        } else {
            renamed += source[i++];
        }
    }
    source = std::move(renamed);
}
}  // namespace

void specializeInputBindings(const Graph &graph, const FusedGroup &group, KernelSpec &spec) {
    std::map<int, int> first;
    for (int i = 0; i < spec.inputCount; ++i) {
        const auto [it, inserted] = first.emplace(storageNode(graph, group.inputs[static_cast<size_t>(i)]), i);
        (void)inserted;
        spec.inputRepresentatives.push_back(it->second);
    }
    if (spec.twoPass) specializeSource(spec.pass1, spec.inputRepresentatives, spec.inputsReadPass1);
    if (!spec.pass2.empty()) specializeSource(spec.pass2, spec.inputRepresentatives, spec.inputCount);
}
}  // namespace eve::tensor::wgsl_detail
