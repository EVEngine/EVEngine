#include "filesystem/physfs/FileApi.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "physfs/physfs.h"

namespace eve {
namespace filesystem {
namespace physfs {
namespace {

bool readViaPhysFS(const char* path, std::string& out) {
    if (!PHYSFS_isInit() || !PHYSFS_exists(path)) return false;
    PHYSFS_file* f = PHYSFS_openRead(path);
    if (!f) return false;
    const PHYSFS_uint64 len = PHYSFS_fileLength(f);
    out.resize(static_cast<size_t>(len));
    if (len > 0 && PHYSFS_readBytes(f, out.data(), len) != len) {
        PHYSFS_close(f);
        out.clear();
        return false;
    }
    PHYSFS_close(f);
    return true;
}

bool readViaOS(const char* path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

bool readFileContent(const char* path, std::string& out) {
    if (readViaPhysFS(path, out) && !out.empty()) return true;
    out.clear();
    return readViaOS(path, out);
}

// Shared implementation of dofile (execute) and loadfile (return closure).
SQInteger compileAndMaybeRun(HSQUIRRELVM vm, bool run) {
    const SQChar* path = nullptr;
    if (SQ_FAILED(sq_getstring(vm, 2, &path)) || !path)
        return sq_throwerror(vm, "expected a script filename");

    std::string content;
    if (!readFileContent(path, content))
        return sq_throwerror(vm, "cannot open script");

    // Squirrel's sq_readscripfile skips a UTF-8 BOM; sq_compilebuffer does not.
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
        content.erase(0, 3);

    if (SQ_FAILED(sq_compilebuffer(vm, content.c_str(), static_cast<SQInteger>(content.size()),
                                   path, SQTrue)))
        return SQ_ERROR;

    if (!run) return 1;  // leave the closure on the stack

    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) return SQ_ERROR;
    sq_pop(vm, 1);
    return 0;
}

SQInteger sq_dofile(HSQUIRRELVM vm) { return compileAndMaybeRun(vm, true); }
SQInteger sq_loadfile(HSQUIRRELVM vm) { return compileAndMaybeRun(vm, false); }

// --- file() handle: a lightweight PhysFS-backed file table ---

bool getThisStr(HSQUIRRELVM vm, const SQChar* field, std::string& out) {
    sq_pushstring(vm, field, -1);
    if (SQ_SUCCEEDED(sq_get(vm, 1))) {
        const SQChar* s = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(vm, -1, &s)) && s) out = s;
        sq_pop(vm, 1);
        return !out.empty();
    }
    return false;
}

SQInteger sq_f_close(HSQUIRRELVM vm) {
    sq_pushnull(vm);
    return 1;
}

SQInteger sq_f_read(HSQUIRRELVM vm) {
    std::string data;
    if (!getThisStr(vm, _SC("_data"), data)) return sq_throwerror(vm, "file not readable");
    sq_pushstring(vm, data.c_str(), static_cast<SQInteger>(data.size()));
    return 1;
}

SQInteger sq_f_getSize(HSQUIRRELVM vm) {
    std::string data;
    if (!getThisStr(vm, _SC("_data"), data)) return sq_throwerror(vm, "file not readable");
    sq_pushinteger(vm, static_cast<SQInteger>(data.size()));
    return 1;
}

SQInteger sq_f_isEOF(HSQUIRRELVM vm) {
    sq_pushbool(vm, SQTrue);
    return 1;
}

SQInteger sq_f_tell(HSQUIRRELVM vm) {
    sq_pushinteger(vm, 0);
    return 1;
}

SQInteger sq_f_seek(HSQUIRRELVM vm) {
    sq_pushbool(vm, SQTrue);
    return 1;
}

SQInteger sq_f_getFilename(HSQUIRRELVM vm) {
    std::string path;
    getThisStr(vm, _SC("_path"), path);
    sq_pushstring(vm, path.c_str(), static_cast<SQInteger>(path.size()));
    return 1;
}

SQInteger sq_file(HSQUIRRELVM vm) {
    const SQChar* path = nullptr;
    const SQChar* mode = nullptr;
    if (SQ_FAILED(sq_getstring(vm, 2, &path)) || SQ_FAILED(sq_getstring(vm, 3, &mode)))
        return sq_throwerror(vm, "file: expected (path, mode)");

    std::string m(mode ? mode : "");
    const bool writing = m.find('w') != std::string::npos || m.find('a') != std::string::npos;

    std::string content;
    if (!writing) {
        readFileContent(path, content);
        if (content.empty() && !PHYSFS_isInit()) return sq_throwerror(vm, "cannot open file");
    }

    sq_newtable(vm);                       // the handle table (becomes 'this')
    sq_pushstring(vm, path, -1);           // _path
    sq_newslot(vm, -3, SQFalse);
    sq_pushstring(vm, content.c_str(), static_cast<SQInteger>(content.size()));  // _data
    sq_newslot(vm, -3, SQFalse);

    struct { const SQChar* name; SQFUNCTION fn; } methods[] = {
        {_SC("close"), sq_f_close},
        {_SC("read"), sq_f_read},
        {_SC("readString"), sq_f_read},
        {_SC("getSize"), sq_f_getSize},
        {_SC("isEOF"), sq_f_isEOF},
        {_SC("tell"), sq_f_tell},
        {_SC("seek"), sq_f_seek},
        {_SC("getFilename"), sq_f_getFilename},
    };
    for (const auto& mth : methods) {
        sq_pushstring(vm, mth.name, -1);
        sq_newclosure(vm, mth.fn, 0);
        sq_newslot(vm, -3, SQFalse);
    }
    return 1;
}

}  // namespace

void installScriptFileApi(ssq::VM& vm) {
    HSQUIRRELVM h = vm.getHandle();
    const SQInteger top = sq_gettop(h);

    struct { const SQChar* name; SQFUNCTION fn; } globals[] = {
        {_SC("dofile"), sq_dofile},
        {_SC("loadfile"), sq_loadfile},
        {_SC("file"), sq_file},
    };
    for (const auto& g : globals) {
        sq_pushroottable(h);
        sq_pushstring(h, g.name, -1);
        sq_newclosure(h, g.fn, 0);
        sq_newslot(h, -3, SQTrue);
        sq_pop(h, 1);
    }

    sq_settop(h, top);
}

}  // namespace physfs
}  // namespace filesystem
}  // namespace eve
