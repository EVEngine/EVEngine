#pragma once

// Windows crash-backtrace support (backward-cpp / DbgHelp). Installed by both
// the engine entry point (src/engine/main.cpp) and the unit-test runner
// (test/main.cpp) so any unhandled exception prints a symbolized stack trace
// instead of a bare exit code. Host-only; the backward.hpp include and the
// EVBacktrace link target are guarded to Windows.

#if defined(EVENGINE_WINDOWS) || defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <werapi.h>
#include <backward.hpp>

#ifndef WER_FAULT_REPORTING_NO_UI
#define WER_FAULT_REPORTING_NO_UI 0x20
#endif
#ifndef WER_FAULT_REPORTING_ALWAYS_SHOW_UI
#define WER_FAULT_REPORTING_ALWAYS_SHOW_UI 0x10
#endif

#include "common/CrashLog.h"

#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>

// backward.hpp pulls in <imagehlp.h> with its own packing; declaring the one
// DbgHelp entry point we call directly avoids including dbghelp.h again. Match
// imagehlp.h's dllimport linkage or MSVC warns C4273.
extern "C" __declspec(dllimport) BOOL WINAPI SymInitialize(
    HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess);

namespace eve {

/**
 * @brief Unhandled-exception filter: print the exception code and a symbolized
 * stack trace (backward-cpp / DbgHelp), then let the OS terminate as usual.
 * @param ep Exception pointers delivered by the OS exception dispatcher.
 * @return EXCEPTION_CONTINUE_SEARCH (we only report, never swallow).
 */
inline LONG WINAPI crashHandler(EXCEPTION_POINTERS *ep) {
    // DbgHelp must be initialized before StackWalk64, otherwise the walk
    // produces garbage frames. Ignore the "already initialized" failure.
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    std::ostringstream report;
    report << "[crash] code=0x" << std::hex << std::setw(8) << std::setfill('0')
           << ep->ExceptionRecord->ExceptionCode << std::dec << std::setfill(' ')
           << " at " << ep->ExceptionRecord->ExceptionAddress << "\n";
    try {
        backward::StackTrace st;
        // Walk from the handler's own frame: the exception dispatch ran on the
        // crashing thread's stack, so the crash site is still in the chain.
        // Walking from ep->ContextRecord produced garbage frames (0xCC) with
        // DbgHelp StackWalk64 on this setup.
        st.load_here(64);
        backward::Printer p;
        p.snippet = false;
        p.color_mode = backward::ColorMode::never;
        p.print(st, report);
    } catch (...) {
        report << "[crash] backtrace unavailable\n";
    }
    const std::string text = report.str();
    std::fputs(text.c_str(), stderr);
    std::fflush(stderr);
    eve::recordCrashEvent(text);
    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * @brief Install the crash backtrace filter and suppress Windows crash UI.
 *
 * Unattended `eve` / `unit_test` runs otherwise stall behind a modal
 * "has stopped working" (or abort) dialog for every access violation.
 *
 * Do not set SEM_NOGPFAULTERRORBOX: that flag skips Windows Error Reporting
 * entirely, so LocalDumps and WER reports never run. Hide the WER UI instead
 * and keep abort's _CALL_REPORTFAULT bit so dumps still get collected.
 */
inline void installCrashHandler() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    DWORD werFlags = 0;
    if (FAILED(WerGetFlags(GetCurrentProcess(), &werFlags))) {
        werFlags = 0;
    }
    (void)WerSetFlags((werFlags | WER_FAULT_REPORTING_NO_UI) &
                      ~static_cast<DWORD>(WER_FAULT_REPORTING_ALWAYS_SHOW_UI));
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
#endif
    SetUnhandledExceptionFilter(&crashHandler);
}

}  // namespace eve

#endif  // EVENGINE_WINDOWS || _WIN32
