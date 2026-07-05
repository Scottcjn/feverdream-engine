// SPDX-License-Identifier: MIT
// fd_retro.h -- Feverdream Engine: CPU old-school post pass.
//
// The GPU path (fd_post.cu, FD_GPU=1) already crunches frames to a VGA look
// with posterize + 4x4 Bayer dither. But most players run the CPU path, where
// the only retro effect was SDL's nearest-neighbour upscale -- pixelation with
// full 24-bit colour, which reads as "low-res modern", not "old". This gives the
// CPU path the same era-correct crunch, computed in-place on the RGBA8 internal
// framebuffer before it is handed to SDL, with no new dependency (header-only,
// <cstdint>/<cstdlib>/<cstring> only -- no CUDA, no GPU, no toolkit).
//
// Effects, in the order a real VGA/CRT chain applied them:
//   1. Ordered 4x4 Bayer dither, so gradients break into era-correct stipple
//      instead of banding when the colour depth drops.
//   2. Colour-depth reduction, one of three modes (FD_RETRO_PALETTE):
//        0 posterize   -- N even levels per channel (soft, tunable)
//        1 vga332      -- 8/8/4 levels (the 256-colour mode-13h cube)
//        2 ega16       -- snap to the 16 fixed RGBI colours (harshest, oldest)
//   3. CRT scanlines, darkening odd rows (FD_RETRO_SCANLINE, 0..100).
//
// Toggle: FD_RETRO=0 disables (default on). The transform is deterministic --
// same pixels + config -> same output -- so it is unit-testable off-target.

#ifndef FD_RETRO_H
#define FD_RETRO_H

#include <cstdint>
#include <cstdlib>
#include <cstring>

struct FdRetro {
    bool enabled = true;
    int  levels  = 6;    // posterize levels per channel (mode 0), >= 2
    int  palette = 0;    // 0 posterize, 1 vga332, 2 ega16
    int  scanline = 35;  // odd-row darkening, 0..100 (0 = off)
};

// The 16 classic EGA/CGA RGBI colours (mode 2). Ordered low-intensity then
// high-intensity, matching the hardware's I-bit brightness pairs.
static const uint8_t FD_EGA16[16][3] = {
    {  0,   0,   0}, {  0,   0, 170}, {  0, 170,   0}, {  0, 170, 170},
    {170,   0,   0}, {170,   0, 170}, {170,  85,   0}, {170, 170, 170},
    { 85,  85,  85}, { 85,  85, 255}, { 85, 255,  85}, { 85, 255, 255},
    {255,  85,  85}, {255,  85, 255}, {255, 255,  85}, {255, 255, 255},
};

static inline int fd_retro_clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// 4x4 ordered dither matrix, returned in [-0.5, 0.5) -- same matrix and range as
// the GPU present kernel so the two paths dither identically.
static inline float fd_retro_bayer4(int x, int y) {
    static const int m[16] = { 0, 8, 2,10, 12, 4,14, 6, 3,11, 1, 9, 15, 7,13, 5 };
    return m[(y & 3) * 4 + (x & 3)] / 16.0f - 0.5f;
}

// Quantize one channel to `levels` even steps (levels >= 2), rounding to nearest.
static inline uint8_t fd_retro_posterize_ch(int v, int levels) {
    int steps = levels - 1;
    int q = (v * steps + 127) / 255;          // 0..steps, rounded
    return (uint8_t)((q * 255 + steps / 2) / steps);
}

// Snap an RGB triple to the nearest EGA16 colour (squared distance).
static inline void fd_retro_snap_ega16(int r, int g, int b,
                                       uint8_t* or_, uint8_t* og, uint8_t* ob) {
    int best = 0; long bestd = 1L << 60;
    for (int i = 0; i < 16; ++i) {
        long dr = r - FD_EGA16[i][0], dg = g - FD_EGA16[i][1], db = b - FD_EGA16[i][2];
        long d = dr * dr + dg * dg + db * db;
        if (d < bestd) { bestd = d; best = i; }
    }
    *or_ = FD_EGA16[best][0]; *og = FD_EGA16[best][1]; *ob = FD_EGA16[best][2];
}

// Apply the retro chain in place to an RGBA8 buffer (w*h*4 bytes). Alpha is
// left untouched. Safe to call every frame; O(w*h).
static inline void fd_retro_apply(uint8_t* rgba, int w, int h, const FdRetro& cfg) {
    if (!cfg.enabled || !rgba || w <= 0 || h <= 0) return;

    // Per-channel levels for the dither amplitude: mode 1 (vga332) dithers R,G at
    // 8 levels and B at 4; posterize uses cfg.levels on all three; ega16 dithers
    // lightly against the fixed set so gradients still stipple before snapping.
    const int lr = cfg.palette == 1 ? 8 : (cfg.palette == 2 ? 6 : cfg.levels);
    const int lg = lr;
    const int lb = cfg.palette == 1 ? 4 : (cfg.palette == 2 ? 6 : cfg.levels);

    const float sl = cfg.scanline <= 0 ? 1.0f
                   : 1.0f - (float)fd_retro_clampi(cfg.scanline, 0, 100) / 100.0f * 0.6f;

    for (int y = 0; y < h; ++y) {
        float rowmul = (cfg.scanline > 0 && (y & 1)) ? sl : 1.0f;
        for (int x = 0; x < w; ++x) {
            uint8_t* px = rgba + ((size_t)y * w + x) * 4;
            int r = px[0], g = px[1], b = px[2];

            // 1. dither: nudge by up to one quantization step before reduction
            float d = fd_retro_bayer4(x, y);
            r = fd_retro_clampi((int)(r + d * (255.0f / (lr - 1))), 0, 255);
            g = fd_retro_clampi((int)(g + d * (255.0f / (lg - 1))), 0, 255);
            b = fd_retro_clampi((int)(b + d * (255.0f / (lb - 1))), 0, 255);

            // 2. colour-depth reduction
            if (cfg.palette == 2) {
                uint8_t nr, ng, nb;
                fd_retro_snap_ega16(r, g, b, &nr, &ng, &nb);
                r = nr; g = ng; b = nb;
            } else {
                r = fd_retro_posterize_ch(r, lr);
                g = fd_retro_posterize_ch(g, lg);
                b = fd_retro_posterize_ch(b, lb);
            }

            // 3. scanline darkening on odd rows
            if (rowmul != 1.0f) {
                r = (int)(r * rowmul); g = (int)(g * rowmul); b = (int)(b * rowmul);
            }
            px[0] = (uint8_t)r; px[1] = (uint8_t)g; px[2] = (uint8_t)b;
        }
    }
}

// Build a config from the environment. FD_RETRO=0 disables; FD_RETRO_LEVELS,
// FD_RETRO_PALETTE (0/1/2), FD_RETRO_SCANLINE (0..100) tune it.
static inline FdRetro fd_retro_from_env() {
    FdRetro c;
    const char* e;
    if ((e = getenv("FD_RETRO")))          c.enabled  = strcmp(e, "0") != 0;
    if ((e = getenv("FD_RETRO_LEVELS")))   c.levels   = fd_retro_clampi(atoi(e), 2, 64);
    if ((e = getenv("FD_RETRO_PALETTE")))  c.palette  = fd_retro_clampi(atoi(e), 0, 2);
    if ((e = getenv("FD_RETRO_SCANLINE"))) c.scanline = fd_retro_clampi(atoi(e), 0, 100);
    return c;
}

#endif // FD_RETRO_H
