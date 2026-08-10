// SPDX-License-Identifier: MIT
// vfe.h (test stub) -- a stand-in for POV-Ray 3.7's vfe.h holding ONLY the
// symbols daemon/fd-daemon.cpp actually names. No POV-Ray code is copied here
// and nothing links against libpovray, so this file stays on the MIT side of
// the firewall (ARCHITECTURE.md -> Licensing) exactly like game/fd_platform.h.
//
// Why it exists: fd-daemon can only be built against a vendored, patched
// POV-Ray tree, which CI cannot produce. Everything above the renderer -- the
// wire protocol, the framing, the declare whitelist, the timeout -- is
// independent of that tree, and this stub lets it be compiled and driven for
// real. See tests/run.sh.
//
// The fake session is not a renderer: it parses the same Width/Height/Clock/
// Declare option strings the real vfe consumes and paints a deterministic
// gradient through the daemon's own display creator, so the framebuffer path
// is exercised end to end and the tests can assert on actual pixels.
#ifndef FD_STUB_VFE_H
#define FD_STUB_VFE_H
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <cmath>

namespace pov_frontend {

class Display {
public:
    struct RGBA8 { unsigned char red, green, blue, alpha; };
    Display(unsigned w, unsigned h) : m_w(w), m_h(h) {}
    virtual ~Display() {}
    unsigned int GetWidth() const  { return m_w; }
    unsigned int GetHeight() const { return m_h; }
    virtual void Initialise() {}
    virtual void DrawPixel(unsigned int, unsigned int, const RGBA8&) {}
    virtual void DrawPixelBlock(unsigned int, unsigned int, unsigned int, unsigned int, const RGBA8*) {}
protected:
    unsigned m_w, m_h;
};

} // namespace pov_frontend

typedef int GammaCurvePtr;

namespace vfe {

using pov_frontend::Display;

class vfeSession;

class vfeDisplay : public pov_frontend::Display {
public:
    vfeDisplay(unsigned int w, unsigned int h, GammaCurvePtr, vfeSession*, bool)
        : pov_frontend::Display(w, h) {}
};

enum vfeStatus { vfeNoError = 0, vfeFailed = 1 };
enum { stRenderShutdown = 0x40 };

class vfeRenderOptions {
public:
    void SetThreadCount(int n) { threads = n; }
    void AddLibraryPath(const std::string& p) { libpaths.push_back(p); }
    void SetSourceFile(const std::string& f) { source = f; }
    void AddCommand(const std::string& c) { commands.push_back(c); }
    int threads = 0;
    std::string source;
    std::vector<std::string> libpaths, commands;
};

typedef vfeDisplay* (*DisplayCreator)(unsigned int, unsigned int, GammaCurvePtr, vfeSession*, bool);

// Fake renderer: parses Width/Height/Clock/Declare out of the options exactly
// as the real vfe would, then paints a deterministic pattern through the
// daemon's own display creator so the framebuffer path is really exercised.
class vfeSession {
public:
    virtual ~vfeSession() { delete disp; }
    int Initialize(void*, void*) { return vfeNoError; }
    const char* GetErrorString() { return err.c_str(); }
    void SetDisplayCreator(DisplayCreator c) { creator = c; }
    void Shutdown() {}

    int SetOptions(const vfeRenderOptions& o) {
        w = h = 0; clock_val = 0.f; declares.clear(); source = o.source;
        for (const std::string& c : o.commands) {
            if (!c.compare(0, 6, "Width="))  w = atoi(c.c_str() + 6);
            else if (!c.compare(0, 7, "Height=")) h = atoi(c.c_str() + 7);
            else if (!c.compare(0, 6, "Clock="))  clock_val = (float)atof(c.c_str() + 6);
            else if (!c.compare(0, 8, "Declare=")) declares.push_back(c.substr(8));
        }
        if (getenv("FD_STUB_FAIL_SETOPTIONS")) { err = "stub: SetOptions refused"; return vfeFailed; }
        // The scene file must exist and be non-empty, like a real parse.
        FILE* f = fopen(source.c_str(), "rb");
        if (!f) { err = "stub: cannot open scene " + source; return vfeFailed; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f);
        scene_bytes = n;
        if (n <= 0) { err = "stub: empty scene"; return vfeFailed; }
        if (w < 1 || h < 1) { err = "stub: bad dimensions"; return vfeFailed; }
        return vfeNoError;
    }

    int StartRender() {
        if (getenv("FD_STUB_FAIL_RENDER")) { err = "stub: render refused"; return vfeFailed; }
        delete disp; disp = creator ? creator((unsigned)w, (unsigned)h, 0, this, false) : nullptr;
        if (!disp) { err = "stub: no display"; return vfeFailed; }
        disp->Initialise();
        // deterministic gradient + clock so tests can assert on real pixels
        std::vector<Display::RGBA8> row((size_t)w);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Display::RGBA8 c;
                c.red   = (unsigned char)(x * 255 / (w > 1 ? w - 1 : 1));
                c.green = (unsigned char)(y * 255 / (h > 1 ? h - 1 : 1));
                c.blue  = (unsigned char)((int)(clock_val * 255.f) & 0xFF);
                c.alpha = 255;
                row[(size_t)x] = c;
            }
            disp->DrawPixelBlock(0, (unsigned)y, (unsigned)(w - 1), (unsigned)y, row.data());
        }
        done = true;
        return vfeNoError;
    }

    int GetStatus(bool, int) { return done ? stRenderShutdown : 0; }

    int w = 0, h = 0; float clock_val = 0.f; long scene_bytes = 0;
    std::vector<std::string> declares; std::string source, err = "stub: no error";
    DisplayCreator creator = nullptr; vfeDisplay* disp = nullptr; bool done = false;
};

} // namespace vfe
#endif
