// SPDX-License-Identifier: MIT
// fd_post.cu -- Feverdream Engine: GPU post pipeline (Phase 3 foundation).
//
// Runs on the local RTX 4070. Two kernels:
//   1. temporal accumulate: history = alpha*current + (1-alpha)*history at
//      internal res. The HOST supplies alpha per frame from sim velocity --
//      analytic motion knowledge instead of motion estimation (GAME_ENGINE §5):
//      still camera -> low alpha (deep accumulation, AA-like), moving -> high
//      alpha (fresh pixels, no ghosting).
//   2. present: bilinear upscale to output res + posterize + 4x4 ordered
//      Bayer dither. The VGA crunch, now computed instead of nearest-sampled.
//
// Built as a standalone .so; fd-game dlopens it when FD_GPU=1. The game never
// links CUDA at build time -- no toolkit, no GPU, no flag => unchanged path.
//
//   nvcc -O3 -shared -Xcompiler -fPIC fd_post.cu -o libfdpost.so

#include <cstdint>
#include <cstdio>
#include <cuda_runtime.h>

namespace {

int g_inW = 0, g_inH = 0, g_outW = 0, g_outH = 0;
uint8_t* d_in = NULL;        // uploaded current frame (RGBA8, in res)
float*   d_hist = NULL;      // accumulation history (RGB float, in res)
uint8_t* d_out = NULL;       // presented frame (RGBA8, out res)
bool     g_first = true;

__global__ void k_accumulate(const uint8_t* in, float* hist,
                             int w, int h, float alpha, int first)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h) return;
    int i = (y * w + x);
    float r = in[i * 4 + 0], g = in[i * 4 + 1], b = in[i * 4 + 2];
    if (first) {
        hist[i * 3 + 0] = r; hist[i * 3 + 1] = g; hist[i * 3 + 2] = b;
    } else {
        hist[i * 3 + 0] = alpha * r + (1.f - alpha) * hist[i * 3 + 0];
        hist[i * 3 + 1] = alpha * g + (1.f - alpha) * hist[i * 3 + 1];
        hist[i * 3 + 2] = alpha * b + (1.f - alpha) * hist[i * 3 + 2];
    }
}

__device__ float bayer4(int x, int y)
{
    // 4x4 ordered dither matrix, normalized to [-0.5, 0.5)
    const int m[16] = { 0, 8, 2,10, 12, 4,14, 6, 3,11, 1, 9, 15, 7,13, 5 };
    return m[(y & 3) * 4 + (x & 3)] / 16.0f - 0.5f;
}

__global__ void k_present(const float* hist, uint8_t* out,
                          int inW, int inH, int outW, int outH, int levels)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= outW || y >= outH) return;

    // bilinear sample from history
    float fx = (x + 0.5f) * inW / outW - 0.5f;
    float fy = (y + 0.5f) * inH / outH - 0.5f;
    int x0 = max(0, min(inW - 1, (int)floorf(fx)));
    int y0 = max(0, min(inH - 1, (int)floorf(fy)));
    int x1 = min(inW - 1, x0 + 1), y1 = min(inH - 1, y0 + 1);
    float tx = fx - x0, ty = fy - y0;
    if (tx < 0) tx = 0; if (ty < 0) ty = 0;

    float step = 255.0f / (levels - 1);
    int o = (y * outW + x) * 4;
    for (int c = 0; c < 3; ++c) {
        float p00 = hist[(y0 * inW + x0) * 3 + c], p10 = hist[(y0 * inW + x1) * 3 + c];
        float p01 = hist[(y1 * inW + x0) * 3 + c], p11 = hist[(y1 * inW + x1) * 3 + c];
        float v = (p00 * (1 - tx) + p10 * tx) * (1 - ty)
                + (p01 * (1 - tx) + p11 * tx) * ty;
        // posterize with ordered dither: quantize AFTER adding the bayer bias
        v = roundf((v + bayer4(x, y) * step) / step) * step;
        out[o + c] = (uint8_t)fminf(255.f, fmaxf(0.f, v));
    }
    out[o + 3] = 255;
}

} // namespace

extern "C" {

void fdpost_shutdown(void);

// returns 0 on success; any CUDA failure leaves the engine on the CPU path
int fdpost_init(int inW, int inH, int outW, int outH)
{
    if (inW <= 0 || inH <= 0 || outW <= 0 || outH <= 0) return -1;
    g_inW = inW; g_inH = inH; g_outW = outW; g_outH = outH; g_first = true;
    if (cudaMalloc(&d_in,   (size_t)inW * inH * 4)                 != cudaSuccess ||
        cudaMalloc(&d_hist, (size_t)inW * inH * 3 * sizeof(float)) != cudaSuccess ||
        cudaMalloc(&d_out,  (size_t)outW * outH * 4)               != cudaSuccess) {
        fdpost_shutdown();      // a partial init must not leak device memory
        return -2;
    }
    cudaDeviceProp prop;
    if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess)
        fprintf(stderr, "fd_post: GPU %s, %d.%d, accumulating %dx%d -> %dx%d\n",
                prop.name, prop.major, prop.minor, inW, inH, outW, outH);
    return 0;
}

// in: RGBA8 inW*inH | alpha: 0..1 blend weight for the CURRENT frame
// levels: posterize levels per channel (e.g. 32 = VGA-ish) | out: RGBA8 out res
int fdpost_frame(const uint8_t* in, float alpha, int levels, uint8_t* out)
{
    if (!d_in || !in || !out || levels < 2) return -1;
    if (cudaMemcpy(d_in, in, (size_t)g_inW * g_inH * 4,
                   cudaMemcpyHostToDevice) != cudaSuccess) return -2;
    dim3 b(16, 16);
    dim3 ga((g_inW + 15) / 16, (g_inH + 15) / 16);
    k_accumulate<<<ga, b>>>(d_in, d_hist, g_inW, g_inH, alpha, g_first ? 1 : 0);
    g_first = false;
    dim3 gp((g_outW + 15) / 16, (g_outH + 15) / 16);
    k_present<<<gp, b>>>(d_hist, d_out, g_inW, g_inH, g_outW, g_outH, levels);
    if (cudaMemcpy(out, d_out, (size_t)g_outW * g_outH * 4,
                   cudaMemcpyDeviceToHost) != cudaSuccess) return -3;
    return cudaGetLastError() == cudaSuccess ? 0 : -4;
}

void fdpost_reset(void) { g_first = true; }   // e.g. on scene/teleport cuts

void fdpost_shutdown(void)
{
    if (d_in)   cudaFree(d_in);
    if (d_hist) cudaFree(d_hist);
    if (d_out)  cudaFree(d_out);
    d_in = NULL; d_hist = NULL; d_out = NULL;
}

}
