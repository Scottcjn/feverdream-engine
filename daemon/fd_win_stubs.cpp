// SPDX-License-Identifier: AGPL-3.0-or-later
// fd_win_stubs.cpp — the Windows platform hooks fd-daemon must supply as POV-Ray's
// custom (headless) front end. On Linux these live in vfe/unix; on Windows the
// pvengine GUI app normally provides them. fd-daemon replaces pvengine, so it
// provides the minimal honest versions here. Links into the AGPL daemon binary.
//
// Signatures must match vfe/win/syspovconfig.h (povwin) and windows/pvfrontend.h
// (pov_frontend) exactly, or the link won't resolve.

#include <cstdlib>
#include <cstddef>
#include <cstring>

namespace povwin
{
    // POV-Windows ships a custom pool allocator for speed; the plain CRT heap is
    // functionally identical and thread-safe, which is all the daemon needs.
    void *win_malloc(size_t size)            { return std::malloc(size); }
    void *win_calloc(size_t n, size_t size)  { return std::calloc(n, size); }
    void *win_realloc(void *p, size_t size)  { return std::realloc(p, size); }
    void  win_free(void *p)                  { std::free(p); }
    char *win_strdup(const char *s)          { return _strdup(s); }

    void  WinMemStage(bool /*beginRender*/, void * /*cookie*/) {}
    void  WinMemThreadStartup() {}
    void  WinMemThreadCleanup() {}

    // No custom accounting — report "nothing to show". uint64=unsigned long long,
    // int64=long long on Win64 (identical mangling to POV's uint64/int64).
    bool  WinMemReport(bool /*global*/, unsigned long long& allocs, unsigned long long& frees,
                       long long& current, unsigned long long& peak,
                       unsigned long long& smallest, unsigned long long& largest)
    {
        allocs = frees = peak = smallest = largest = 0;
        current = 0;
        return false;
    }
}

namespace pov_frontend
{
    // Shellouts run external programs around a render. A headless render daemon
    // must never do that — refuse, and minimize any shellout machinery.
    bool MinimizeShellouts(void)  { return true; }
    bool ShelloutsPermitted(void) { return false; }
}
