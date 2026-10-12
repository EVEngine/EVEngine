#pragma once
// Internal owning values shared by the scanner and graph projection. No world pointers.
#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "common/ScriptStructure.hpp"

namespace eve::dev::project_index {
using Object = Poco::JSON::Object;
struct Function {
    std::string name, body;
    unsigned    line = 1;
};
struct File {
    eve::script::ScriptStructure structure;
    std::string                  hash, text, error;
    Poco::Dynamic::Var           json;
    std::vector<Function>        functions;
    std::vector<std::string>     references;
};
using Files = std::map<std::string, std::shared_ptr<const File>>;
struct Snapshot {
    bool                               automatic = false;
    Files                              files;
    std::string                        projectId, revision, projectDir;
    std::map<std::string, Object::Ptr> nodes;
    std::vector<Object::Ptr>           edges, diagnostics;
    Object::Ptr                        stats;
};
/** @brief SHA-256 evidence digest; not a runtime frame or binary identity. */
std::string digest(const std::string& text);
/** @brief UTF-8 source path, including Windows project names. */
std::string pathText(const std::filesystem::path& path);
/** @brief Construct a native path from UTF-8 source/tool arguments. */
inline std::filesystem::path sourcePath(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
/** @brief Bounded filesystem scan. Parses only changed files; symlinks are excluded. */
Files scan(const std::filesystem::path& root, const Files& previous, Object::Ptr stats);
/** @brief Build a candidate evidence graph without touching the published snapshot. */
Snapshot project(Files files, Object::Ptr stats, std::string defaultProjectId);
/** @brief Project typed compiler syntax through generated engine contracts; no project declarations required. */
void automaticGraph(Snapshot& snapshot, std::string& version);
/** @brief Strict source string fields; invalid manifest shapes throw a diagnostic exception. */
std::string requiredString(Object::Ptr object, const std::string& field);
}  // namespace eve::dev::project_index
