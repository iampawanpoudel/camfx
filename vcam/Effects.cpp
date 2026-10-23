#include "Effects.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    inline uint8_t Clamp8(float v) { return (uint8_t)(v <= 0.f ? 0 : v >= 255.f ? 255 : (int)(v + 0.5f)); }
    inline float Clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

    // Separable box filter, borders normalized by the number of samples. in may alias out.
    void BoxBlur(const float* in, float* out, float* tmp, int w, int h, int r)
    {
        for (int y = 0; y < h; y++)
        {
            const float* row = in + (size_t)y * w;
            float* t = tmp + (size_t)y * w;
            float sum = 0.f; int cnt = 0;
            for (int x = 0; x <= r && x < w; x++) { sum += row[x]; cnt++; }
            for (int x = 0; x < w; x++)
            {
                t[x] = sum / cnt;
                if (x + r + 1 < w) { sum += row[x + r + 1]; cnt++; }
                if (x - r >= 0) { sum -= row[x - r]; cnt--; }
            }
        }
        for (int x = 0; x < w; x++)
        {
            float sum = 0.f; int cnt = 0;
            for (int y = 0; y <= r && y < h; y++) { sum += tmp[(size_t)y * w + x]; cnt++; }
            for (int y = 0; y < h; y++)
            {
                out[(size_t)y * w + x] = sum / cnt;
                if (y + r + 1 < h) { sum += tmp[(size_t)(y + r + 1) * w + x]; cnt++; }
                if (y - r >= 0) { sum -= tmp[(size_t)(y - r) * w + x]; cnt--; }
            }
        }
    }

    // Map full-res coordinate to quarter-res sample index + weight.
    inline void LowResCoord(int x, int lw, int& x0, float& f)
    {
        float u = (x + 0.5f) * 0.25f - 0.5f;
        if (u < 0.f) u = 0.f;
        x0 = std::min((int)u, std::max(lw - 2, 0));
        f = std::min(u - x0, 1.f);
    }

    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
}

void BuildLuts(const FxSettings& s, uint8_t lutY[256], uint8_t lutU[256], uint8_t lutV[256])
{
    float gain = std::pow(2.f, s.exposure / 100.f);
    float bright = s.brightness / 100.f * 0.25f;
    float contrast = 1.f + s.contrast / 100.f;
    float gamma = 1.f, lift = 0.f, range = 1.f;
    float sat = 1.f + s.saturation / 100.f;
    float du = -12.f * s.warmth / 100.f, dv = 12.f * s.warmth / 100.f;

    if (s.lowLight) { gain *= 1.35f; gamma = 0.8f; }
    switch (s.filter)
    {
    case FilterMono:  sat = 0.f; break;
    case FilterWarm:  du -= 8.f; dv += 8.f; break;
    case FilterCool:  du += 8.f; dv -= 6.f; break;
    case FilterVivid: sat *= 1.35f; contrast *= 1.15f; break;
    case FilterSepia: sat = 0.f; du -= 10.f; dv += 12.f; break;
    case FilterFade:  sat *= 0.8f; lift = 0.08f; range = 0.84f; break;
    }

    for (int i = 0; i < 256; i++)
    {
        float x = i / 255.f * gain + bright;
        x = (x - 0.5f) * contrast + 0.5f;
        x = Clamp01(x);
        if (gamma != 1.f) x = std::pow(x, gamma);
        x = lift + x * range;
        lutY[i] = Clamp8(x * 255.f);
        lutU[i] = Clamp8(128.f + (i - 128) * sat + du);
        lutV[i] = Clamp8(128.f + (i - 128) * sat + dv);
    }
}

FxRect ZoomRect(int w, int h, float zoom, float panX, float panY)
{
    zoom = std::clamp(zoom, 1.f, 4.f);
    float cw = w / zoom, ch = h / zoom;
    float cx = (w - cw) * 0.5f * (1.f + std::clamp(panX, -1.f, 1.f));
    float cy = (h - ch) * 0.5f * (1.f + std::clamp(panY, -1.f, 1.f));
    return { cx, cy, cw, ch };
}

void Effects::SetSettings(const FxSettings& s)
{
    if (s.autoFrame && !m_s.autoFrame)
    {
        m_frameRect = m_frameTarget = { 0.f, 0.f, (float)m_w, (float)m_h };
    }
    m_s = s;
    BuildLuts(m_s, m_lutY, m_lutU, m_lutV);
    m_lutIdentity = true;
    for (int i = 0; i < 256; i++)
    {
        if (m_lutY[i] != i || m_lutU[i] != i || m_lutV[i] != i) { m_lutIdentity = false; break; }
    }

    // Low light is split out of the main LUT when an image background is used: the person gets
    // it before compositing, so the replacement image keeps its own exposure.
    FxSettings noLl = m_s;
    noLl.lowLight = false;
    BuildLuts(noLl, m_lutYNoLl, m_lutUNoLl, m_lutVNoLl);
    m_lutNoLlIdentity = true;
    for (int i = 0; i < 256; i++)
    {
        if (m_lutYNoLl[i] != i || m_lutUNoLl[i] != i || m_lutVNoLl[i] != i) { m_lutNoLlIdentity = false; break; }
    }

    FxSettings boost;
    boost.lowLight = true;
    uint8_t boostU[256], boostV[256];
    BuildLuts(boost, m_lutBoostY, boostU, boostV);
}

void Effects::BoostForeground(uint8_t* y, int yPitch, int w, int h)
{
    for (int yy = 0; yy < h; yy++)
    {
        uint8_t* p = y + (size_t)yy * yPitch;
        const uint8_t* ar = &m_alpha[(size_t)yy * m_w];
        for (int x = 0; x < w; x++)
            if (ar[x]) p[x] = m_lutBoostY[p[x]];
    }
}

void Effects::SetBackgroundImage(std::vector<uint8_t> bgra, int w, int h)
{
    m_bgra = std::move(bgra);
    m_bgraW = w; m_bgraH = h;
    m_imgW = m_imgH = 0; // refit on next frame
}

void Effects::Resize(int w, int h)
{
    m_w = w; m_h = h;
    m_lw = (w + 3) / 4; m_lh = (h + 3) / 4;
    size_t n = (size_t)m_lw * m_lh;
    for (auto* v : { &m_I, &m_p, &m_A, &m_B, &m_q, &m_t0, &m_t1, &m_t2, &m_t3, &m_tmp }) v->assign(n, 0.f);
    for (int c = 0; c < 3; c++) { m_bg[c].assign(n, 0.f); m_bgPlain[c].assign(n, 0.f); }
    m_cell.assign(n, 0);
    m_alpha.assign((size_t)w * h, 255);
    m_x0.resize(w); m_fx.resize(w);
    for (int x = 0; x < w; x++) LowResCoord(x, m_lw, m_x0[x], m_fx[x]);
    m_rgb.assign(3 * kMaskW * kMaskH, 0.f);
    m_mask.assign(kMaskW * kMaskH, 0.f);
    m_maskEma.assign(kMaskW * kMaskH, 0.f);
    m_haveMask = false;
    m_frameRect = m_frameTarget = { 0.f, 0.f, (float)w, (float)h };
    m_imgW = m_imgH = 0;
}

bool Effects::UpdateMask(const uint8_t* y, int yPitch, const uint8_t* uv, int uvPitch)
{
    if (!m_segment) return false;
    const int MW = kMaskW, MH = kMaskH, plane = MW * MH;
    for (int i = 0; i < MH; i++)
    {
        int sy = std::min((int)((i + 0.5f) * m_h / MH), m_h - 1);
        const uint8_t* yr = y + (size_t)sy * yPitch;
        const uint8_t* uvr = uv + (size_t)(sy / 2) * uvPitch;
        for (int j = 0; j < MW; j++)
        {
            int sx = std::min((int)((j + 0.5f) * m_w / MW), m_w - 1);
            float Y = 1.164f * (yr[sx] - 16), U = uvr[sx & ~1] - 128.f, V = uvr[(sx & ~1) + 1] - 128.f;
            m_rgb[i * MW + j] = Clamp01((Y + 1.596f * V) / 255.f);
            m_rgb[plane + i * MW + j] = Clamp01((Y - 0.392f * U - 0.813f * V) / 255.f);
            m_rgb[2 * plane + i * MW + j] = Clamp01((Y + 2.017f * U) / 255.f);
        }
    }
    if (!m_segment(m_rgb.data(), m_mask.data())) return m_haveMask;

    if (!m_haveMask) m_maskEma = m_mask;
    else for (int i = 0; i < plane; i++) m_maskEma[i] = Lerp(m_maskEma[i], m_mask[i], 0.4f); // temporal smoothing: 0.6 flickered at edges once inference ran every frame
    m_haveMask = true;
    return true;
}

// Fast guided filter: fit the coarse model mask to edges of the current frame at quarter res,
// then upsample the linear coefficients and apply them against the full-res luma.
void Effects::BuildAlpha(const uint8_t* y, int yPitch)
{
    const int lw = m_lw, lh = m_lh, MW = kMaskW, MH = kMaskH;
    for (int i = 0; i < lh; i++)
    {
        float my = std::clamp((i + 0.5f) * MH / lh - 0.5f, 0.f, MH - 1.001f);
        int my0 = (int)my; float fy = my - my0; int my1 = std::min(my0 + 1, MH - 1);
        for (int j = 0; j < lw; j++)
        {
            int sum = 0, cnt = 0;
            for (int yy = i * 4; yy < std::min(i * 4 + 4, m_h); yy++)
            {
                const uint8_t* r = y + (size_t)yy * yPitch;
                for (int xx = j * 4; xx < std::min(j * 4 + 4, m_w); xx++) { sum += r[xx]; cnt++; }
            }
            m_I[i * lw + j] = sum / (255.f * cnt);

            float mx = std::clamp((j + 0.5f) * MW / lw - 0.5f, 0.f, MW - 1.001f);
            int mx0 = (int)mx; float fx = mx - mx0; int mx1 = std::min(mx0 + 1, MW - 1);
            const float* m = m_maskEma.data();
            m_p[i * lw + j] = Lerp(Lerp(m[my0 * MW + mx0], m[my0 * MW + mx1], fx), Lerp(m[my1 * MW + mx0], m[my1 * MW + mx1], fx), fy);
        }
    }

    const size_t n = (size_t)lw * lh;
    // Larger eps keeps the fit close to the model mask: small eps lets camera noise through as spikes at edges.
    const int r = 2; const float eps = 2e-2f;
    for (size_t k = 0; k < n; k++) { m_t2[k] = m_I[k] * m_p[k]; m_t3[k] = m_I[k] * m_I[k]; }
    BoxBlur(m_I.data(), m_t0.data(), m_tmp.data(), lw, lh, r);
    BoxBlur(m_p.data(), m_t1.data(), m_tmp.data(), lw, lh, r);
    BoxBlur(m_t2.data(), m_t2.data(), m_tmp.data(), lw, lh, r);
    BoxBlur(m_t3.data(), m_t3.data(), m_tmp.data(), lw, lh, r);
    for (size_t k = 0; k < n; k++)
    {
        float a = (m_t2[k] - m_t0[k] * m_t1[k]) / (m_t3[k] - m_t0[k] * m_t0[k] + eps);
        m_A[k] = a;
        m_B[k] = m_t1[k] - a * m_t0[k];
    }
    BoxBlur(m_A.data(), m_A.data(), m_tmp.data(), lw, lh, r);
    BoxBlur(m_B.data(), m_B.data(), m_tmp.data(), lw, lh, r);
    for (size_t k = 0; k < n; k++) m_q[k] = Clamp01(m_A[k] * m_I[k] + m_B[k]);

    // Only cells near the person's edge need per-pixel refinement; the rest are solid 0 or 255.
    for (int i = 0; i < lh; i++)
    {
        for (int j = 0; j < lw; j++)
        {
            float qmin = 1.f, qmax = 0.f;
            for (int ii = std::max(i - 1, 0); ii <= std::min(i + 1, lh - 1); ii++)
                for (int jj = std::max(j - 1, 0); jj <= std::min(j + 1, lw - 1); jj++)
                {
                    float q = m_q[(size_t)ii * lw + jj];
                    qmin = std::min(qmin, q); qmax = std::max(qmax, q);
                }
            m_cell[(size_t)i * lw + j] = qmax < 0.05f ? 0 : qmin > 0.95f ? 255 : 1;
        }
    }

    for (int yy = 0; yy < m_h; yy++)
    {
        int y0; float fy; LowResCoord(yy, lh, y0, fy);
        int y1 = std::min(y0 + 1, lh - 1);
        const float *A0 = &m_A[(size_t)y0 * lw], *A1 = &m_A[(size_t)y1 * lw];
        const float *B0 = &m_B[(size_t)y0 * lw], *B1 = &m_B[(size_t)y1 * lw];
        const uint8_t* yr = y + (size_t)yy * yPitch;
        const uint8_t* cr = &m_cell[(size_t)(yy / 4) * lw];
        uint8_t* ar = &m_alpha[(size_t)yy * m_w];
        for (int x = 0; x < m_w; x++)
        {
            uint8_t cell = cr[x >> 2];
            if (cell != 1) { ar[x] = cell; continue; }
            int x0 = m_x0[x], x1 = std::min(x0 + 1, lw - 1); float fx = m_fx[x];
            float A = Lerp(Lerp(A0[x0], A0[x1], fx), Lerp(A1[x0], A1[x1], fx), fy);
            float B = Lerp(Lerp(B0[x0], B0[x1], fx), Lerp(B1[x0], B1[x1], fx), fy);
            float a = A * yr[x] * (1.f / 255.f) + B;
            ar[x] = Clamp8((a - 0.25f) * (255.f / 0.6f)); // contrast curve trims the old background's halo
        }
    }
}

void Effects::FitBackgroundImage()
{
    if (m_bgra.empty() || (m_imgW == m_w && m_imgH == m_h)) return;
    m_imgW = m_w; m_imgH = m_h;
    m_imgY.resize((size_t)m_w * m_h);
    m_imgUV.resize((size_t)m_w * (m_h / 2));
    float scale = std::max((float)m_w / m_bgraW, (float)m_h / m_bgraH); // cover-fit
    float ox = (m_bgraW - m_w / scale) * 0.5f, oy = (m_bgraH - m_h / scale) * 0.5f;
    auto px = [&](int x, int y) {
        int sx = std::clamp((int)(ox + (x + 0.5f) / scale), 0, m_bgraW - 1);
        int sy = std::clamp((int)(oy + (y + 0.5f) / scale), 0, m_bgraH - 1);
        return &m_bgra[((size_t)sy * m_bgraW + sx) * 4];
    };
    for (int y = 0; y < m_h; y++)
    {
        for (int x = 0; x < m_w; x++)
        {
            const uint8_t* p = px(x, y); float B = p[0], G = p[1], R = p[2];
            m_imgY[(size_t)y * m_w + x] = Clamp8(16.f + 0.257f * R + 0.504f * G + 0.098f * B);
            if (!(x & 1) && !(y & 1))
            {
                uint8_t* uv = &m_imgUV[(size_t)(y / 2) * m_w + x];
                uv[0] = Clamp8(128.f - 0.148f * R - 0.291f * G + 0.439f * B);
                uv[1] = Clamp8(128.f + 0.439f * R - 0.368f * G - 0.071f * B);
            }
        }
    }
}

void Effects::Composite(uint8_t* y, int yPitch, uint8_t* uv, int uvPitch)
{
    const int lw = m_lw, lh = m_lh;
    const size_t n = (size_t)lw * lh;
    bool useImage = m_s.background == BgImage && !m_bgra.empty();

    if (useImage)
    {
        FitBackgroundImage();
    }
    else
    {
        // Quarter-res Y/U/V, then a person-masked (normalized) blur so the subject doesn't bleed into the background.
        for (int i = 0; i < lh; i++)
        {
            const uint8_t* uvr = uv + (size_t)std::min(i * 2, m_h / 2 - 1) * uvPitch;
            const uint8_t* uvr2 = uv + (size_t)std::min(i * 2 + 1, m_h / 2 - 1) * uvPitch;
            for (int j = 0; j < lw; j++)
            {
                int c0 = std::min(j * 2, m_w / 2 - 1) * 2, c1 = std::min(j * 2 + 1, m_w / 2 - 1) * 2;
                size_t k = (size_t)i * lw + j;
                m_bgPlain[0][k] = m_I[k] * 255.f;
                m_bgPlain[1][k] = (uvr[c0] + uvr[c1] + uvr2[c0] + uvr2[c1]) * 0.25f;
                m_bgPlain[2][k] = (uvr[c0 + 1] + uvr[c1 + 1] + uvr2[c0 + 1] + uvr2[c1 + 1]) * 0.25f;
            }
        }
        static const int kRadius[5] = { 1, 2, 3, 5, 7 };
        int r = kRadius[std::clamp(m_s.blur, 1, 5) - 1];
        for (size_t k = 0; k < n; k++) m_t0[k] = 1.f - m_q[k];
        for (int c = 0; c < 3; c++)
            for (size_t k = 0; k < n; k++) m_bg[c][k] = m_bgPlain[c][k] * m_t0[k];
        for (int it = 0; it < 2; it++)
        {
            BoxBlur(m_t0.data(), m_t0.data(), m_tmp.data(), lw, lh, r);
            for (int c = 0; c < 3; c++)
            {
                BoxBlur(m_bg[c].data(), m_bg[c].data(), m_tmp.data(), lw, lh, r);
                BoxBlur(m_bgPlain[c].data(), m_bgPlain[c].data(), m_tmp.data(), lw, lh, r);
            }
        }
        for (size_t k = 0; k < n; k++)
        {
            float den = m_t0[k], t = Clamp01(den * 20.f);
            for (int c = 0; c < 3; c++)
                m_bg[c][k] = Lerp(m_bgPlain[c][k], den > 1e-4f ? m_bg[c][k] / den : 0.f, t);
        }

        // Upsample along x once per quarter-res row, so each output pixel below needs a single lerp.
        for (int c = 0; c < 3; c++)
        {
            const int n = c ? m_w / 2 : m_w, step = c ? 2 : 1;
            m_hrow[c].resize((size_t)lh * n);
            for (int i = 0; i < lh; i++)
            {
                const float* b = &m_bg[c][(size_t)i * lw];
                float* h = &m_hrow[c][(size_t)i * n];
                for (int x = 0; x < n; x++)
                {
                    int sx = x * step, x0 = m_x0[sx], x1 = std::min(x0 + 1, lw - 1);
                    h[x] = Lerp(b[x0], b[x1], m_fx[sx]);
                }
            }
        }
    }

    for (int yy = 0; yy < m_h; yy++)
    {
        int y0; float fy; LowResCoord(yy, lh, y0, fy);
        int y1 = std::min(y0 + 1, lh - 1);
        uint8_t* yr = y + (size_t)yy * yPitch;
        const uint8_t* ar = &m_alpha[(size_t)yy * m_w];
        const uint8_t* ir = useImage ? &m_imgY[(size_t)yy * m_w] : nullptr;
        const float* h0 = useImage ? nullptr : &m_hrow[0][(size_t)y0 * m_w];
        const float* h1 = useImage ? nullptr : &m_hrow[0][(size_t)y1 * m_w];
        for (int x = 0; x < m_w; x++)
        {
            int a = ar[x];
            if (a == 255) continue;
            float bg = useImage ? ir[x] : Lerp(h0[x], h1[x], fy);
            yr[x] = Clamp8(bg + (yr[x] - bg) * (a * (1.f / 255.f)));
        }
    }
    const int uw = m_w / 2;
    for (int yu = 0; yu < m_h / 2; yu++)
    {
        int y0; float fy; LowResCoord(yu * 2, lh, y0, fy);
        int y1 = std::min(y0 + 1, lh - 1);
        uint8_t* uvr = uv + (size_t)yu * uvPitch;
        const uint8_t* ar = &m_alpha[(size_t)yu * 2 * m_w];
        const uint8_t* ir = useImage ? &m_imgUV[(size_t)yu * m_w] : nullptr;
        for (int xu = 0; xu < uw; xu++)
        {
            int a = ar[xu * 2];
            if (a == 255) continue;
            float t = a * (1.f / 255.f);
            for (int c = 0; c < 2; c++)
            {
                float bg = useImage ? ir[xu * 2 + c]
                    : Lerp(m_hrow[1 + c][(size_t)y0 * uw + xu], m_hrow[1 + c][(size_t)y1 * uw + xu], fy);
                uvr[xu * 2 + c] = Clamp8(bg + (uvr[xu * 2 + c] - bg) * t);
            }
        }
    }
}

// Center Stage-style framing: keep head and shoulders in view, ease toward the target, ignore small moves.
void Effects::UpdateAutoFrame()
{
    int minX = m_lw, minY = m_lh, maxX = -1, maxY = -1, count = 0;
    for (int i = 0; i < m_lh; i++)
        for (int j = 0; j < m_lw; j++)
            if (m_q[(size_t)i * m_lw + j] > 0.5f)
            {
                minX = std::min(minX, j); maxX = std::max(maxX, j);
                minY = std::min(minY, i); maxY = std::max(maxY, i);
                count++;
            }

    const float W = (float)m_w, H = (float)m_h, sx = W / m_lw, sy = H / m_lh;
    FxRect target{ 0.f, 0.f, W, H };
    if (count > m_lw * m_lh / 100)
    {
        // A seated person's mask runs off the bottom of the frame, so size the crop from width (shoulders).
        float top = minY * sy, bw = (maxX - minX + 1) * sx;
        float ch = std::clamp(bw * 1.6f * H / W, H * 0.4f, H);
        float cw = ch * W / H;
        float cx = (minX + maxX + 1) * 0.5f * sx - cw * 0.5f;
        float cy = top - ch * 0.08f; // a little headroom
        target = { std::clamp(cx, 0.f, W - cw), std::clamp(cy, 0.f, H - ch), cw, ch };
    }

    const float dead = 0.06f * W;
    if (std::fabs(target.x - m_frameTarget.x) > dead || std::fabs(target.y - m_frameTarget.y) > dead ||
        std::fabs(target.w - m_frameTarget.w) > dead)
    {
        m_frameTarget = target;
    }
    const float k = 0.06f;
    m_frameRect.x = Lerp(m_frameRect.x, m_frameTarget.x, k);
    m_frameRect.y = Lerp(m_frameRect.y, m_frameTarget.y, k);
    m_frameRect.w = Lerp(m_frameRect.w, m_frameTarget.w, k);
    m_frameRect.h = m_frameRect.w * H / W;
}

void Effects::Process(uint8_t* y, int yPitch, uint8_t* uv, int uvPitch, int w, int h)
{
    if (w < 16 || h < 16) return;
    if (w != m_w || h != m_h) Resize(w, h);
    m_frame++;

    bool haveAlpha = false;
    if ((m_s.background != BgOff || m_s.autoFrame) && UpdateMask(y, yPitch, uv, uvPitch))
    {
        BuildAlpha(y, yPitch);
        haveAlpha = true;
    }

    // With a replacement image, low light belongs to the person, not to the image: boost the
    // foreground before compositing and keep low light out of the frame-wide LUT below.
    bool boostForeground = m_s.lowLight && m_s.background == BgImage && haveAlpha;
    if (boostForeground) BoostForeground(y, yPitch, w, h);
    if (haveAlpha && m_s.background != BgOff) Composite(y, yPitch, uv, uvPitch);

    const uint8_t* lutY = boostForeground ? m_lutYNoLl : m_lutY;
    const uint8_t* lutU = boostForeground ? m_lutUNoLl : m_lutU;
    const uint8_t* lutV = boostForeground ? m_lutVNoLl : m_lutV;
    const bool lutIdentity = boostForeground ? m_lutNoLlIdentity : m_lutIdentity;

    FxRect r = ZoomRect(w, h, m_s.zoom, m_s.panX, m_s.panY);
    if (m_s.autoFrame)
    {
        if (haveAlpha) UpdateAutoFrame();
        r = m_frameRect;
    }

    bool resample = m_s.mirror || r.w < w - 0.5f || r.h < h - 0.5f;
    if (!resample)
    {
        if (lutIdentity) return;
        for (int yy = 0; yy < h; yy++)
        {
            uint8_t* p = y + (size_t)yy * yPitch;
            for (int x = 0; x < w; x++) p[x] = lutY[p[x]];
        }
        for (int yy = 0; yy < h / 2; yy++)
        {
            uint8_t* p = uv + (size_t)yy * uvPitch;
            for (int x = 0; x < (w & ~1); x += 2) { p[x] = lutU[p[x]]; p[x + 1] = lutV[p[x + 1]]; }
        }
        return;
    }

    // Copy source planes, then bilinear-sample the crop rect back into the frame (8-bit fixed point weights).
    m_srcY.resize((size_t)w * h);
    m_srcUV.resize((size_t)w * (h / 2));
    for (int yy = 0; yy < h; yy++) memcpy(&m_srcY[(size_t)yy * w], y + (size_t)yy * yPitch, w);
    for (int yy = 0; yy < h / 2; yy++) memcpy(&m_srcUV[(size_t)yy * w], uv + (size_t)yy * uvPitch, w);

    auto tables = [&](int n, float rx, float rw, int srcN) {
        m_rx.resize(n); m_rf.resize(n);
        float s = rw / n;
        for (int j = 0; j < n; j++)
        {
            int jj = m_s.mirror ? n - 1 - j : j;
            float f = std::clamp(rx + (jj + 0.5f) * s - 0.5f, 0.f, srcN - 1.001f);
            m_rx[j] = (int)f;
            m_rf[j] = (int)((f - (int)f) * 256.f);
        }
    };
    auto rowCoord = [](int i, int n, float ry, float rh, int srcN, int& y0, int& fy) {
        float f = std::clamp(ry + (i + 0.5f) * (rh / n) - 0.5f, 0.f, srcN - 1.001f);
        y0 = (int)f; fy = (int)((f - y0) * 256.f);
    };

    tables(w, r.x, r.w, w);
    for (int i = 0; i < h; i++)
    {
        int y0, fy; rowCoord(i, h, r.y, r.h, h, y0, fy);
        const uint8_t* a = &m_srcY[(size_t)y0 * w];
        const uint8_t* b = a + (y0 + 1 < h ? w : 0);
        uint8_t* out = y + (size_t)i * yPitch;
        for (int j = 0; j < w; j++)
        {
            int x0 = m_rx[j], fx = m_rf[j];
            int top = a[x0] * (256 - fx) + a[x0 + 1] * fx;
            int bot = b[x0] * (256 - fx) + b[x0 + 1] * fx;
            out[j] = lutY[(top * (256 - fy) + bot * fy + 32768) >> 16];
        }
    }

    const int uw = w / 2, uh = h / 2;
    tables(uw, r.x * 0.5f, r.w * 0.5f, uw);
    for (int i = 0; i < uh; i++)
    {
        int y0, fy; rowCoord(i, uh, r.y * 0.5f, r.h * 0.5f, uh, y0, fy);
        const uint8_t* a = &m_srcUV[(size_t)y0 * w];
        const uint8_t* b = a + (y0 + 1 < uh ? w : 0);
        uint8_t* out = uv + (size_t)i * uvPitch;
        for (int j = 0; j < uw; j++)
        {
            int x0 = m_rx[j] * 2, fx = m_rf[j];
            for (int c = 0; c < 2; c++)
            {
                int top = a[x0 + c] * (256 - fx) + a[x0 + 2 + c] * fx;
                int bot = b[x0 + c] * (256 - fx) + b[x0 + 2 + c] * fx;
                int v = (top * (256 - fy) + bot * fy + 32768) >> 16;
                out[j * 2 + c] = c ? lutV[v] : lutU[v];
            }
        }
    }
}
