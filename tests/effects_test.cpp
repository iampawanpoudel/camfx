// Plain self-checking test for the effects pipeline. Exit code 0 = pass.
#include "Effects.h"
#include "Segmenter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

struct Frame
{
    int w, h;
    std::vector<uint8_t> y, uv;
    Frame(int w_, int h_, uint8_t Y, uint8_t U, uint8_t V) : w(w_), h(h_), y((size_t)w_ * h_, Y), uv((size_t)w_ * (h_ / 2))
    {
        for (size_t i = 0; i < uv.size(); i += 2) { uv[i] = U; uv[i + 1] = V; }
    }
    void Run(Effects& fx) { fx.Process(y.data(), w, uv.data(), w, w, h); }
};

static void TestLuts()
{
    FxSettings s;
    uint8_t ly[256], lu[256], lv[256];
    BuildLuts(s, ly, lu, lv);
    bool identity = true;
    for (int i = 0; i < 256; i++) identity &= ly[i] == i && lu[i] == i && lv[i] == i;
    CHECK(identity);

    s.filter = FilterMono;
    BuildLuts(s, ly, lu, lv);
    CHECK(lu[0] == 128 && lu[255] == 128 && lv[37] == 128);
    CHECK(ly[200] == 200);

    s = FxSettings(); s.brightness = 100;
    BuildLuts(s, ly, lu, lv);
    CHECK(ly[100] > 100 && ly[255] == 255);

    s = FxSettings(); s.warmth = 100;
    BuildLuts(s, ly, lu, lv);
    CHECK(lu[128] < 128 && lv[128] > 128); // warmer = less blue, more red

    // Every preset must change something; Perpetua is empty in the source list, so it is the identity.
    for (int f = Filter_1977; f < FilterCount; f++)
    {
        s = FxSettings(); s.filter = f;
        BuildLuts(s, ly, lu, lv);
        bool changed = false;
        for (int i = 0; i < 256; i++) changed |= ly[i] != i || lu[i] != i || lv[i] != i;
        CHECK(changed == (f != FilterPerpetua));
    }

    s = FxSettings(); s.filter = FilterInkwell; // full grayscale
    BuildLuts(s, ly, lu, lv);
    CHECK(lu[100] == 128 && lv[200] == 128 && ly[200] > 200);

    s = FxSettings(); s.filter = FilterKelvin; // amber overlay tint: less blue, more red
    BuildLuts(s, ly, lu, lv);
    CHECK(lu[128] < 128 && lv[128] > 128);
}

static void TestZoomRect()
{
    FxRect r = ZoomRect(1280, 720, 2.f, 0.f, 0.f);
    CHECK(r.x == 320 && r.y == 180 && r.w == 640 && r.h == 360);
    r = ZoomRect(1280, 720, 2.f, 1.f, -1.f);
    CHECK(r.x == 640 && r.y == 0);
    r = ZoomRect(1280, 720, 1.f, 0.5f, 0.5f);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1280 && r.h == 720);
    r = ZoomRect(1280, 720, 10.f, 0.f, 0.f); // clamped to 4x
    CHECK(r.w == 320);
}

static void TestPassthrough()
{
    Effects fx;
    fx.SetSettings(FxSettings());
    Frame f(64, 32, 90, 100, 150);
    f.y[5] = 7;
    Frame orig = f;
    f.Run(fx);
    CHECK(f.y == orig.y && f.uv == orig.uv);
}

static void TestMirrorAndZoom()
{
    Effects fx;
    FxSettings s; s.mirror = true;
    fx.SetSettings(s);
    Frame f(64, 32, 0, 128, 128);
    for (int y = 0; y < f.h; y++) for (int x = 0; x < f.w; x++) f.y[y * f.w + x] = (uint8_t)(x * 4);
    f.Run(fx);
    CHECK(f.y[0] == 252 && f.y[63] == 0);

    Effects fz;
    s = FxSettings(); s.zoom = 2.f;
    fz.SetSettings(s);
    Frame g(64, 32, 0, 128, 128);
    for (int y = 0; y < g.h; y++) for (int x = 0; x < g.w; x++) g.y[y * g.w + x] = (uint8_t)(x * 4);
    g.Run(fz);
    // 2x center crop covers source x in [16, 48): output spans ~64..188
    CHECK(std::abs(g.y[0] - 64) <= 3 && std::abs(g.y[63] - 188) <= 3);
}

static void TestBackground()
{
    const int W = 64, H = 32;
    std::vector<uint8_t> red((size_t)8 * 8 * 4);
    for (size_t i = 0; i < red.size(); i += 4) { red[i] = 0; red[i + 1] = 0; red[i + 2] = 255; red[i + 3] = 255; }

    FxSettings s; s.background = BgImage;
    // Mask = 0 everywhere: the whole frame becomes the background image (red: Y~82, V~240).
    Effects bg;
    bg.SetSettings(s);
    bg.SetBackgroundImage(red, 8, 8);
    bg.SetSegmenter([](const float*, float* m) { for (int i = 0; i < Effects::kMaskW * Effects::kMaskH; i++) m[i] = 0.f; return true; });
    Frame f(W, H, 200, 128, 128);
    f.Run(bg);
    CHECK(std::abs(f.y[W * 10 + 10] - 82) <= 2);
    CHECK(std::abs(f.uv[W * 5 + 21] - 240) <= 2);

    // Mask = 1 everywhere: the person covers the frame, nothing changes.
    Effects fg;
    fg.SetSettings(s);
    fg.SetBackgroundImage(red, 8, 8);
    fg.SetSegmenter([](const float*, float* m) { for (int i = 0; i < Effects::kMaskW * Effects::kMaskH; i++) m[i] = 1.f; return true; });
    Frame g(W, H, 200, 128, 128);
    Frame orig = g;
    g.Run(fg);
    CHECK(g.y == orig.y && g.uv == orig.uv);

    // Blur of a flat frame stays flat, at every strength.
    s.background = BgBlur;
    for (s.blur = 1; s.blur <= 5; s.blur++)
    {
        Effects bl;
        bl.SetSettings(s);
        bl.SetSegmenter([](const float*, float* m) { for (int i = 0; i < Effects::kMaskW * Effects::kMaskH; i++) m[i] = 0.f; return true; });
        Frame b(W, H, 120, 90, 160);
        b.Run(bl);
        CHECK(std::abs(b.y[W * 16 + 32] - 120) <= 1 && std::abs(b.uv[W * 8 + 32] - 90) <= 1);
    }
}

// argv[1]: folder with onnxruntime.dll and person_segmenter.onnx
static void TestSegmenter(const wchar_t* dir)
{
    { Segmenter unused; } // destroying without Init must not touch the ONNX Runtime API

    Segmenter seg;
    CHECK(seg.Init(dir));
    std::vector<float> rgb(3 * Effects::kMaskW * Effects::kMaskH, 0.5f), mask(Effects::kMaskW * Effects::kMaskH, -1.f);
    CHECK(seg.Run(rgb.data(), mask.data()));
    bool inRange = true;
    for (float v : mask) inRange &= v >= 0.f && v <= 1.f;
    CHECK(inRange);
}

// camfx_tests <model dir> --bench : per-frame cost of the full pipeline at 720p
static void Bench(const wchar_t* dir)
{
    Segmenter seg;
    if (!seg.Init(dir)) { std::printf("bench: segmenter init failed\n"); return; }
    {
        std::vector<float> rgb(3 * Effects::kMaskW * Effects::kMaskH, 0.5f), mask(Effects::kMaskW * Effects::kMaskH);
        seg.Run(rgb.data(), mask.data());
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 30; i++) seg.Run(rgb.data(), mask.data());
        std::printf("bench %-24s %6.2f ms/run\n", "model inference", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 30);
    }
    struct Case { const char* name; FxSettings s; } cases[4];
    cases[0].name = "colour only"; cases[0].s.filter = FilterVivid;
    cases[1].name = "zoom 2x"; cases[1].s.zoom = 2.f;
    cases[2].name = "blur"; cases[2].s.background = BgBlur;
    cases[3].name = "blur+autoframe+filter"; cases[3].s.background = BgBlur; cases[3].s.autoFrame = true; cases[3].s.filter = FilterWarm;
    for (auto& c : cases)
    {
        Effects fx;
        fx.SetSettings(c.s);
        fx.SetSegmenter([&](const float* rgb, float* m) { return seg.Run(rgb, m); });
        Frame f(1280, 720, 100, 120, 140);
        for (size_t i = 0; i < f.y.size(); i++) f.y[i] = (uint8_t)(i * 7 % 251);
        const int n = 120;
        auto t0 = std::chrono::steady_clock::now();
        const Frame src = f; // Process works in place: restart from the same input each run, like a real camera
        for (int i = 0; i < n; i++) { f.y = src.y; f.uv = src.uv; f.Run(fx); }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
        std::printf("bench %-24s %6.2f ms/frame  (%4.1f%% of one core at 30 fps)\n", c.name, ms, ms * 30 / 10);
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc > 2 && !wcscmp(argv[2], L"--bench")) { Bench(argv[1]); return 0; }
    TestLuts();
    TestZoomRect();
    TestPassthrough();
    TestMirrorAndZoom();
    TestBackground();
    if (argc > 1) TestSegmenter(argv[1]);
    else std::printf("skipping segmenter test (no model dir)\n");
    std::printf(g_fail ? "%d check(s) failed\n" : "all effects tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
