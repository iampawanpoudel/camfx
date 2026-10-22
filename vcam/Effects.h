// CamFX effects pipeline: works in place on NV12 frames. No COM/MF here so it can be unit tested.
#pragma once
#include "Segmenter.h"
#include <cstdint>
#include <functional>
#include <vector>

enum FxFilter { FilterNone, FilterMono, FilterWarm, FilterCool, FilterVivid, FilterSepia, FilterFade, FilterCount };
enum FxBackground { BgOff, BgBlur, BgImage };

struct FxSettings
{
    float zoom = 1.f;               // 1..4
    float panX = 0.f, panY = 0.f;   // -1..1
    bool autoFrame = false;
    bool mirror = false;
    int background = BgOff;
    int blur = 3;                   // 1..5, 1 = lightest
    int brightness = 0, contrast = 0, saturation = 0, warmth = 0, exposure = 0; // -100..100
    int filter = FilterNone;
    bool lowLight = false;
};

struct FxRect { float x, y, w, h; };

// Pure helpers, exposed for tests.
void BuildLuts(const FxSettings& s, uint8_t lutY[256], uint8_t lutU[256], uint8_t lutV[256]);
FxRect ZoomRect(int w, int h, float zoom, float panX, float panY);

class Effects
{
public:
    static constexpr int kMaskW = Segmenter::kW, kMaskH = Segmenter::kH;
    // Input: planar RGB float 0..1, 3 x kMaskH x kMaskW. Output: person alpha 0..1, kMaskH x kMaskW.
    using SegmentFn = std::function<bool(const float* rgb, float* mask)>;

    void SetSettings(const FxSettings& s);
    const FxSettings& Settings() const { return m_s; }
    void SetSegmenter(SegmentFn fn) { m_segment = std::move(fn); }
    // BGRA 8-bit image used for BgImage; cover-fitted to the frame size on first use.
    void SetBackgroundImage(std::vector<uint8_t> bgra, int w, int h);
    void Process(uint8_t* y, int yPitch, uint8_t* uv, int uvPitch, int w, int h);

private:
    void Resize(int w, int h);
    bool UpdateMask(const uint8_t* y, int yPitch, const uint8_t* uv, int uvPitch);
    void BuildAlpha(const uint8_t* y, int yPitch);
    void Composite(uint8_t* y, int yPitch, uint8_t* uv, int uvPitch);
    void UpdateAutoFrame();
    void FitBackgroundImage();

    FxSettings m_s;
    SegmentFn m_segment;
    uint8_t m_lutY[256], m_lutU[256], m_lutV[256];
    bool m_lutIdentity = true;

    int m_w = 0, m_h = 0, m_lw = 0, m_lh = 0;  // frame and quarter-res sizes
    unsigned m_frame = 0;
    bool m_haveMask = false;

    std::vector<float> m_rgb, m_mask, m_maskEma;      // model input / output / smoothed
    std::vector<float> m_I, m_p, m_A, m_B, m_q, m_t0, m_t1, m_t2, m_t3, m_tmp; // quarter-res guided filter
    std::vector<float> m_bg[3], m_bgPlain[3];         // quarter-res blurred Y, U, V
    std::vector<float> m_hrow[3];                     // blurred bg upsampled along x, per quarter-res row
    std::vector<uint8_t> m_cell;                      // quarter-res: 0 / 255 solid, 1 = edge
    std::vector<uint8_t> m_alpha;                     // full-res person alpha
    std::vector<int> m_x0; std::vector<float> m_fx;   // quarter->full x interpolation tables
    std::vector<int> m_rx, m_rf;                      // zoom resample x tables (Y then UV)
    std::vector<uint8_t> m_srcY, m_srcUV;             // copy of frame when resampling

    std::vector<uint8_t> m_bgra; int m_bgraW = 0, m_bgraH = 0;
    std::vector<uint8_t> m_imgY, m_imgUV; int m_imgW = 0, m_imgH = 0;

    FxRect m_frameRect{}, m_frameTarget{};            // auto-framing state
};
