#include <windows.h>

// DbgHelp declarations require the Win32 types above.
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
BOOL CALLBACK source(PSOURCEFILE file, PVOID) {
    if (std::strstr(file->FileName, "ccache-verification-src")) std::printf("SOURCE %s\n", file->FileName);
    return TRUE;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEBUG);
    if (!SymInitialize(process, nullptr, FALSE)) return 3;
    auto base = SymLoadModuleEx(process, nullptr, argv[1], nullptr, 0, 0, nullptr, 0);
    if (!base) {
        std::printf("load error %lu\n", GetLastError());
        return 4;
    }
    if (!SymEnumSourceFiles(process, base, nullptr, source, nullptr)) {
        std::printf("enum error %lu\n", GetLastError());
        return 5;
    }
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 1024]{};
    auto*                     symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct             = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen               = 1024;
    if (!SymFromName(process, "main", symbol)) return 6;
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct  = sizeof(line);
    DWORD displacement = 0;
    if (!SymGetLineFromAddr64(process, symbol->Address, &displacement, &line)) return 7;
    std::printf("MAIN %s:%lu\n", line.FileName, line.LineNumber);
    SymCleanup(process);
}
