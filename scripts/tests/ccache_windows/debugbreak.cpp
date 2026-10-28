#include <windows.h>

// DbgHelp declarations require the Win32 types above.
#include <dbghelp.h>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::string         command = "\"" + std::string(argv[1]) + "\"";
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process))
        return 3;
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process.hProcess, nullptr, FALSE);
    DWORD64     address  = 0;
    BYTE        original = 0;
    bool        stopped = false, failed = false, exited = false;
    DEBUG_EVENT event{};
    while (WaitForDebugEvent(&event, 15000)) {
        DWORD status = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            auto base =
                SymLoadModuleEx(process.hProcess, event.u.CreateProcessInfo.hFile, argv[1], nullptr,
                                reinterpret_cast<DWORD64>(event.u.CreateProcessInfo.lpBaseOfImage), 0, nullptr, 0);
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 1024]{};
            auto*                     symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct             = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen               = 1024;
            if (!base || !SymFromName(process.hProcess, "main", symbol)) {
                failed = true;
                break;
            }
            address           = symbol->Address;
            SIZE_T bytes      = 0;
            BYTE   breakpoint = 0xcc;
            if (!ReadProcessMemory(process.hProcess, reinterpret_cast<void*>(address), &original, 1, &bytes) ||
                !WriteProcessMemory(process.hProcess, reinterpret_cast<void*>(address), &breakpoint, 1, &bytes)) {
                failed = true;
                break;
            }
            FlushInstructionCache(process.hProcess, reinterpret_cast<void*>(address), 1);
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            auto& exception = event.u.Exception.ExceptionRecord;
            if (exception.ExceptionCode == EXCEPTION_BREAKPOINT &&
                reinterpret_cast<DWORD64>(exception.ExceptionAddress) == address) {
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct  = sizeof(line);
                DWORD displacement = 0;
                if (!SymGetLineFromAddr64(process.hProcess, address, &displacement, &line)) {
                    failed = true;
                    break;
                }
                std::string path = line.FileName, producer = argv[2], consumer = argv[3];
                if (path.starts_with(producer)) {
                    path = consumer + path.substr(producer.size());
                }
                FILE* source = nullptr;
                fopen_s(&source, path.c_str(), "rb");
                if (!source) {
                    failed = true;
                    break;
                }
                fclose(source);
                if (!path.starts_with(consumer)) {
                    failed = true;
                    break;
                }
                std::printf("BREAKPOINT %s:%lu\n", path.c_str(), line.LineNumber);
                stopped      = true;
                SIZE_T bytes = 0;
                if (!WriteProcessMemory(process.hProcess, reinterpret_cast<void*>(address), &original, 1, &bytes)) {
                    failed = true;
                    break;
                }
                HANDLE  thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, event.dwThreadId);
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (!thread || !GetThreadContext(thread, &context)) {
                    if (thread) CloseHandle(thread);
                    failed = true;
                    break;
                }
                context.Rip = address;
                if (!SetThreadContext(thread, &context)) failed = true;
                CloseHandle(thread);
                FlushInstructionCache(process.hProcess, reinterpret_cast<void*>(address), 1);
            } else if (exception.ExceptionCode != EXCEPTION_BREAKPOINT) {
                status = DBG_EXCEPTION_NOT_HANDLED;
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            exited = true;
            failed = event.u.ExitProcess.dwExitCode != 0;
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
            break;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
    }
    if (!exited) failed = true;
    if (!stopped || failed) TerminateProcess(process.hProcess, 9);
    SymCleanup(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return stopped && !failed ? 0 : 4;
}
