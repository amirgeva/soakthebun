#pragma once

// A tiny anti-aliased software rasterizer. All game art (tiles, buns, the
// water-drop hero, the app icon) is drawn procedurally with it at whatever
// resolution the window needs, so the game ships with no image files.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace soak {

struct RGBA {
    float r, g, b, a;
};

inline RGBA rgb(uint32_t hex, float alpha = 1.f) {
    return RGBA{float((hex >> 16) & 255) / 255.f, float((hex >> 8) & 255) / 255.f, float(hex & 255) / 255.f, alpha};
}

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline RGBA mix(RGBA a, RGBA b, float t) {
    return RGBA{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h), px_(size_t(w) * size_t(h), RGBA{0, 0, 0, 0}) {}

    int width() const { return w_; }
    int height() const { return h_; }

    // Source-over blend of a straight-alpha colour with partial coverage.
    void blend(int x, int y, const RGBA& c, float coverage) {
        const float a = c.a * coverage;
        if (a <= 0.f) return;
        RGBA& d = px_[size_t(y) * size_t(w_) + size_t(x)];
        const float ia = 1.f - a;
        d.r = c.r * a + d.r * ia;
        d.g = c.g * a + d.g * ia;
        d.b = c.b * a + d.b * ia;
        d.a = a + d.a * ia;
    }

    // Straight-alpha RGBA bytes (R, G, B, A order in memory).
    std::vector<uint8_t> toRGBA8() const;

private:
    int w_, h_;
    std::vector<RGBA> px_;  // premultiplied
};

// Shape tests in unit space.
inline bool inCircle(float x, float y, float cx, float cy, float r) {
    const float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

inline bool inEllipse(float x, float y, float cx, float cy, float rx, float ry, float rot = 0.f) {
    float dx = x - cx, dy = y - cy;
    if (rot != 0.f) {
        const float c = std::cos(rot), s = std::sin(rot);
        const float tx = dx * c + dy * s;
        dy = -dx * s + dy * c;
        dx = tx;
    }
    dx /= rx;
    dy /= ry;
    return dx * dx + dy * dy <= 1.f;
}

inline bool inRoundRect(float x, float y, float x0, float y0, float x1, float y1, float r) {
    if (x < x0 || x > x1 || y < y0 || y > y1) return false;
    const float cx = std::clamp(x, x0 + r, x1 - r);
    const float cy = std::clamp(y, y0 + r, y1 - r);
    return inCircle(x, y, cx, cy, r);
}

// A drop: circle of radius r at (cx, cy) with a point at (cx, apexY).
inline bool inTeardrop(float x, float y, float cx, float cy, float r, float apexY) {
    if (inCircle(x, y, cx, cy, r)) return true;
    const float d = std::fabs(cy - apexY);
    if (d <= r) return false;
    const float dir = apexY < cy ? 1.f : -1.f;  // +1 when the point is above
    const float along = (y - apexY) * dir;      // distance from apex toward the centre
    const float tangent = d - r * r / d;        // where the cone meets the circle
    if (along < 0.f || along > tangent) return false;
    const float tanA = r / std::sqrt(d * d - r * r);
    return std::fabs(x - cx) <= along * tanA;
}

// Paints: colour as a function of unit-space position.
struct Solid {
    RGBA c;
    RGBA operator()(float, float) const { return c; }
};
struct VGrad {
    float y0, y1;
    RGBA a, b;
    RGBA operator()(float, float y) const { return mix(a, b, clamp01((y - y0) / (y1 - y0))); }
};
struct Radial {
    float cx, cy, r;
    RGBA a, b;
    RGBA operator()(float x, float y) const {
        const float dx = x - cx, dy = y - cy;
        return mix(a, b, clamp01(std::sqrt(dx * dx + dy * dy) / r));
    }
};
inline Solid solid(RGBA c) { return Solid{c}; }

// Draws into a canvas through a unit coordinate system:
//   pixel = origin + unit * scale
// A Pen made with Pen(canvas) maps the unit square onto the whole canvas.
class Pen {
public:
    explicit Pen(Canvas& c) : c_(&c), ox_(0.f), oy_(0.f), s_(float(c.width())) {}
    Pen(Canvas& c, float ox, float oy, float scale) : c_(&c), ox_(ox), oy_(oy), s_(scale) {}

    // A pen whose unit square is the given sub-square of this pen's space.
    Pen sub(float x, float y, float scale) const { return Pen(*c_, ox_ + x * s_, oy_ + y * s_, s_ * scale); }

    // Fills every pixel of the unit-space box whose 4x4 subsamples satisfy
    // `inside`, weighted by coverage.
    template <class Inside, class Paint>
    void fill(float ux0, float uy0, float ux1, float uy1, Inside inside, Paint paint) const {
        constexpr int ss = 4;
        const int x0 = std::max(0, int(std::floor(ox_ + ux0 * s_)));
        const int y0 = std::max(0, int(std::floor(oy_ + uy0 * s_)));
        const int x1 = std::min(c_->width(), int(std::ceil(ox_ + ux1 * s_)));
        const int y1 = std::min(c_->height(), int(std::ceil(oy_ + uy1 * s_)));
        const float inv = 1.f / s_;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                int n = 0;
                for (int sy = 0; sy < ss; ++sy) {
                    const float uy = (float(y) + (float(sy) + 0.5f) / ss - oy_) * inv;
                    for (int sx = 0; sx < ss; ++sx) {
                        const float ux = (float(x) + (float(sx) + 0.5f) / ss - ox_) * inv;
                        if (inside(ux, uy)) ++n;
                    }
                }
                if (!n) continue;
                const RGBA col = paint((float(x) + 0.5f - ox_) * inv, (float(y) + 0.5f - oy_) * inv);
                c_->blend(x, y, col, float(n) / float(ss * ss));
            }
        }
    }

    template <class Paint>
    void rect(float x0, float y0, float x1, float y1, Paint p) const {
        fill(x0, y0, x1, y1, [=](float x, float y) { return x >= x0 && x < x1 && y >= y0 && y < y1; }, p);
    }
    template <class Paint>
    void circle(float cx, float cy, float r, Paint p) const {
        fill(cx - r, cy - r, cx + r, cy + r, [=](float x, float y) { return inCircle(x, y, cx, cy, r); }, p);
    }
    template <class Paint>
    void ellipse(float cx, float cy, float rx, float ry, Paint p, float rot = 0.f) const {
        const float m = std::max(rx, ry);
        fill(cx - m, cy - m, cx + m, cy + m, [=](float x, float y) { return inEllipse(x, y, cx, cy, rx, ry, rot); }, p);
    }
    template <class Paint>
    void ring(float cx, float cy, float rx, float ry, float thick, Paint p) const {
        fill(cx - rx - thick, cy - ry - thick, cx + rx + thick, cy + ry + thick,
             [=](float x, float y) {
                 return inEllipse(x, y, cx, cy, rx + thick * 0.5f, ry + thick * 0.5f) &&
                        !inEllipse(x, y, cx, cy, rx - thick * 0.5f, ry - thick * 0.5f);
             },
             p);
    }
    template <class Paint>
    void roundRect(float x0, float y0, float x1, float y1, float r, Paint p) const {
        fill(x0, y0, x1, y1, [=](float x, float y) { return inRoundRect(x, y, x0, y0, x1, y1, r); }, p);
    }
    template <class Paint>
    void teardrop(float cx, float cy, float r, float apexY, Paint p) const {
        fill(cx - r, std::min(apexY, cy - r), cx + r, std::max(apexY, cy + r),
             [=](float x, float y) { return inTeardrop(x, y, cx, cy, r, apexY); }, p);
    }

private:
    Canvas* c_;
    float ox_, oy_, s_;
};

}  // namespace soak
