#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <Poco/SHA2Engine.h>
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
#include "common/ScriptCompiler.h"
#include "devtools/ProjectIndexInternal.hpp"

namespace eve::dev::project_index {
std::string digest(const std::string& text) {
    Poco::SHA2Engine engine;
    engine.update(text);
    return Poco::DigestEngine::digestToHex(engine.digest());
}
std::string pathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}
std::string requiredString(Object::Ptr object, const std::string& field) {
    if (!object || !object->has(field) || !object->get(field).isString())
        throw std::runtime_error("Expected string: " + field);
    auto value = object->getValue<std::string>(field);
    if (value.empty() || value.size() > 4096) throw std::runtime_error("Invalid string: " + field);
    return value;
}
namespace {
// Preserve offsets and line endings, but remove comments and optionally strings.
std::string masked(const std::string& text, bool strings) {
    std::string out  = text;
    bool        line = false, block = false, escaped = false, verbatim = false;
    char        quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i], next = i + 1 < text.size() ? text[i + 1] : 0;
        if (line) {
            if (c == '\n')
                line = false;
            else
                out[i] = ' ';
            continue;
        }
        if (block) {
            if (c != '\n' && c != '\r') out[i] = ' ';
            if (c == '*' && next == '/') {
                out[++i] = ' ';
                block    = false;
            }
            continue;
        }
        if (quote) {
            if (strings && c != '\n' && c != '\r') out[i] = ' ';
            if (escaped) {
                escaped = false;
                continue;
            }
            if (!verbatim && c == '\\') {
                escaped = true;
                continue;
            }
            if (c == quote) {
                if (verbatim && next == quote) {
                    if (strings) out[i + 1] = ' ';
                    ++i;
                } else
                    quote = 0;
            }
            continue;
        }
        if (c == '/' && (next == '/' || next == '*')) {
            out[i] = out[i + 1] = ' ';
            ++i;
            line  = next == '/';
            block = next == '*';
            continue;
        }
        if (c == '"' || c == '\'') {
            quote    = c;
            verbatim = c == '"' && i > 0 && text[i - 1] == '@';
            if (strings) out[i] = ' ';
        }
    }
    return out;
}
std::shared_ptr<const File> parse(const std::string& path, std::string text, const std::string& hash) {
    auto file  = std::make_shared<File>();
    file->text = std::move(text);
    file->hash = hash;
    try {
        const auto extension = std::filesystem::path(path).extension().string();
        if (extension == ".json") {
            Poco::JSON::Parser parser;
            parser.setDepth(64);
            file->json = parser.parse(file->text);
        } else if (extension == ".nut") {
            const auto code     = masked(file->text, true);
            const auto comments = masked(file->text, false);
            const auto metadata = eve::script::ScriptCompiler::analyze(file->text, path);
            file->structure     = metadata.structure;
            if (!file->structure.diagnostics.empty()) file->error = file->structure.diagnostics.front();
            for (const auto& function : file->structure.functions) {
                file->functions.push_back(
                    {function.name, comments.substr(function.begin, function.end - function.begin), function.line});
            }
            static const std::regex reference(R"REF(\bdofile\s*\(\s*"([^"\n]+)"\s*\))REF");
            for (auto it = std::sregex_iterator(comments.begin(), comments.end(), reference);
                 it != std::sregex_iterator(); ++it)
                if (code[static_cast<size_t>(it->position())] == 'd') file->references.push_back((*it)[1].str());
        }
    } catch (const std::exception& e) {
        file->error = e.what();
        file->json  = {};
        file->functions.clear();
        file->structure = {};
    }
    return file;
}
}  // namespace
Files scan(const std::filesystem::path& root, const Files& previous, Object::Ptr stats) {
    Files                       next;
    std::vector<std::string>    newPaths;
    size_t                      total  = 0;
    int                         parsed = 0, reused = 0, added = 0, modified = 0, removed = 0;
    const std::set<std::string> excluded{".git", ".eve", "build", "third-party", "node_modules"};
    for (auto it = std::filesystem::recursive_directory_iterator(root);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        const auto& entry = *it;
        // Also exclude directory junctions/reparse aliases on Windows. The root
        // itself has already been canonicalized by the caller.
        if (entry.is_symlink() || std::filesystem::canonical(entry.path()) != entry.path()) {
            it.disable_recursion_pending();
            continue;
        }
        if (entry.is_directory()) {
            const auto name = pathText(entry.path().filename());
            if (name.starts_with('.') || excluded.contains(name)) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension().string();
        if (extension != ".json" && extension != ".nut" && extension != ".dnut" && extension != ".md") continue;
        const auto size = entry.file_size();
        total += static_cast<size_t>(size);
        if (size > 2 * 1024 * 1024 || total > 32 * 1024 * 1024 || next.size() >= 512)
            throw std::runtime_error("Project input budget exceeded");
        std::ifstream input(entry.path(), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read project source");
        std::string text((std::istreambuf_iterator<char>(input)), {});
        if (input.bad() || text.size() != size) throw std::runtime_error("Project source changed during read");
        const auto path = pathText(entry.path().lexically_relative(root));
        const auto hash = digest(text);
        const auto old  = previous.find(path);
        if (old != previous.end() && old->second->hash == hash) {
            next[path] = old->second;
            ++reused;
        } else if (old != previous.end()) {
            next[path] = parse(path, std::move(text), hash);
            ++parsed;
            ++modified;
        } else {
            auto pending  = std::make_shared<File>();
            pending->hash = hash;
            pending->text = std::move(text);
            next[path]    = pending;
            newPaths.push_back(path);
            ++added;
        }
    }
    Poco::JSON::Array::Ptr                          renames = new Poco::JSON::Array();
    std::map<std::string, std::vector<std::string>> deletedHashes, addedHashes;
    std::set<std::string>                           reusedRenames;
    for (const auto& [path, file] : previous)
        if (!next.contains(path)) {
            ++removed;
            deletedHashes[file->hash].push_back(path);
        }
    for (const auto& [path, file] : next)
        if (!previous.contains(path)) addedHashes[file->hash].push_back(path);
    for (const auto& [hash, paths] : deletedHashes) {
        const auto created = addedHashes.find(hash);
        if (paths.size() != 1 || created == addedHashes.end() || created->second.size() != 1) continue;
        if (std::filesystem::path(paths.front()).extension() !=
            std::filesystem::path(created->second.front()).extension())
            continue;
        Object::Ptr rename = new Object();
        rename->set("from", paths.front());
        rename->set("to", created->second.front());
        renames->add(rename);
        // Content is identical; preserve the parsed records across this rename.
        next[created->second.front()] = previous.at(paths.front());
        ++reused;
        --added;
        --removed;
        reusedRenames.insert(created->second.front());
    }
    for (const auto& path : newPaths)
        if (!reusedRenames.contains(path)) {
            next[path] = parse(path, next.at(path)->text, next.at(path)->hash);
            ++parsed;
        }
    stats->set("parsed", parsed);
    stats->set("reused", reused);
    stats->set("added", added);
    stats->set("modified", modified);
    stats->set("removed", removed);
    stats->set("renames", renames);
    stats->set("files", next.size());
    stats->set("bytes", total);
    return next;
}
}  // namespace eve::dev::project_index
