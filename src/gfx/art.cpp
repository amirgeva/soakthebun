#include "gfx/art.h"

#include "core/level.h"

namespace soak::art {
namespace {

// Water palette shared by puddles, soaked buns, the hero and the icon.
const RGBA kWaterEdge = rgb(0x175A93);
const RGBA kWaterLight = rgb(0xA6E7FF);
const RGBA kWaterDeep = rgb(0x2F8DD6);

bool inPuddle(float x, float y, float g) {
    return inEllipse(x, y, 0.50f, 0.57f, 0.36f + g, 0.24f + g) || inEllipse(x, y, 0.29f, 0.50f, 0.17f + g, 0.14f + g) ||
           inEllipse(x, y, 0.70f, 0.63f, 0.19f + g, 0.15f + g) || inEllipse(x, y, 0.61f, 0.41f, 0.16f + g, 0.12f + g);
}

void waterDrop(const Pen& p, float cx, float cy, float r) {
    p.teardrop(cx, cy, r * 1.25f, cy - r * 3.0f, solid(rgb(0x0E3E73, 0.9f)));
    p.teardrop(cx, cy, r, cy - r * 2.6f, Radial{cx - r * 0.4f, cy - r * 0.5f, r * 1.6f, kWaterLight, kWaterDeep});
    p.ellipse(cx - r * 0.35f, cy - r * 0.25f, r * 0.22f, r * 0.38f, solid(rgb(0xFFFFFF, 0.8f)), 0.4f);
}

}  // namespace

void floorTile(const Pen& p, int variant) {
    p.rect(0.f, 0.f, 1.f, 1.f, solid(rgb(0xC7B192)));
    const RGBA top = variant ? rgb(0xEEE0C8) : rgb(0xF7EEDD);
    const RGBA bottom = variant ? rgb(0xE0CFB2) : rgb(0xECDFC6);
    p.roundRect(0.035f, 0.035f, 0.965f, 0.965f, 0.07f, VGrad{0.f, 1.f, top, bottom});
    p.roundRect(0.10f, 0.08f, 0.50f, 0.12f, 0.02f, solid(rgb(0xFFFFFF, 0.35f)));
}

void wallTile(const Pen& p) {
    p.rect(0.f, 0.f, 1.f, 1.f, solid(rgb(0x2C3646)));
    const float g = 0.035f;
    auto brick = [&](float x0, float y0, float x1, float y1) {
        p.roundRect(x0 + g, y0 + g, x1 - g, y1 - g, 0.05f, VGrad{y0, y1, rgb(0x8E9DB3), rgb(0x56647A)});
        p.roundRect(x0 + g + 0.04f, y0 + g + 0.025f, x1 - g - 0.04f, y0 + g + 0.07f, 0.02f, solid(rgb(0xCCD6E3, 0.55f)));
        p.roundRect(x0 + g + 0.02f, y1 - g - 0.05f, x1 - g - 0.02f, y1 - g - 0.015f, 0.02f, solid(rgb(0x2C3646, 0.35f)));
    };
    brick(0.0f, 0.0f, 0.5f, 0.5f);
    brick(0.5f, 0.0f, 1.0f, 0.5f);
    brick(-0.25f, 0.5f, 0.25f, 1.0f);
    brick(0.25f, 0.5f, 0.75f, 1.0f);
    brick(0.75f, 0.5f, 1.25f, 1.0f);
}

void puddle(const Pen& p) {
    p.fill(0.f, 0.f, 1.f, 1.f, [](float x, float y) { return inPuddle(x, y, 0.03f); }, solid(rgb(0x175A93, 0.9f)));
    p.fill(0.f, 0.f, 1.f, 1.f, [](float x, float y) { return inPuddle(x, y, 0.f); },
           Radial{0.42f, 0.47f, 0.45f, kWaterLight, kWaterDeep});
    p.ring(0.50f, 0.57f, 0.22f, 0.13f, 0.02f, solid(rgb(0xFFFFFF, 0.3f)));
    p.ellipse(0.35f, 0.46f, 0.085f, 0.03f, solid(rgb(0xFFFFFF, 0.8f)), -0.35f);
    p.ellipse(0.67f, 0.60f, 0.045f, 0.018f, solid(rgb(0xFFFFFF, 0.65f)), -0.35f);
}

void ripple(const Pen& p) { p.ring(0.5f, 0.5f, 0.44f, 0.30f, 0.05f, solid(rgb(0xFFFFFF))); }

void dot(const Pen& p) {
    p.circle(0.5f, 0.5f, 0.46f, solid(rgb(0xFFFFFF, 0.55f)));
    p.circle(0.5f, 0.5f, 0.36f, solid(rgb(0xFFFFFF)));
}

void bun(const Pen& p, bool soaked) {
    const float sag = soaked ? 0.05f : 0.f;

    // What it sits on: a soft shadow, or the puddle it is soaking up.
    if (soaked) {
        p.ellipse(0.5f, 0.82f, 0.49f, 0.15f, solid(rgb(0x175A93, 0.9f)));
        p.ellipse(0.5f, 0.82f, 0.46f, 0.125f, Radial{0.40f, 0.78f, 0.5f, kWaterLight, kWaterDeep});
        p.ring(0.5f, 0.83f, 0.40f, 0.09f, 0.015f, solid(rgb(0xFFFFFF, 0.35f)));
    } else {
        p.ellipse(0.5f, 0.85f, 0.41f, 0.08f, solid(rgb(0x000000, 0.25f)));
    }

    const RGBA outline = soaked ? rgb(0x3A2C27) : rgb(0x6E3B12);

    // Lower half of the bun: a paler band under the dome.
    const RGBA bandTop = soaked ? rgb(0xA99482) : rgb(0xF7DCA3);
    const RGBA bandBottom = soaked ? rgb(0x7C6A5C) : rgb(0xE2AC5E);
    p.roundRect(0.07f, 0.54f + sag * 0.5f, 0.93f, 0.83f, 0.13f, solid(outline));
    p.roundRect(0.10f, 0.565f + sag * 0.5f, 0.90f, 0.80f, 0.11f, VGrad{0.56f, 0.80f, bandTop, bandBottom});

    // The dome.
    const float cy = 0.58f + sag;
    const float rx = 0.42f;
    const float ry = soaked ? 0.33f : 0.40f;
    const float cut = 0.645f + sag * 0.5f;
    auto dome = [=](float grow) {
        return [=](float x, float y) { return y <= cut + grow && inEllipse(x, y, 0.5f, cy, rx + grow, ry + grow); };
    };
    p.fill(0.f, 0.f, 1.f, 1.f, dome(0.03f), solid(outline));
    const RGBA hi = soaked ? rgb(0xC09C7C) : rgb(0xFFCB70);
    const RGBA lo = soaked ? rgb(0x6E5243) : rgb(0xC46F1E);
    p.fill(0.f, 0.f, 1.f, 1.f, dome(0.f), Radial{0.38f, cy - ry * 0.6f, 0.55f, hi, lo});

    if (soaked) {
        // Dark, soggy blotches where the water has wicked in.
        p.ellipse(0.62f, cy - ry * 0.15f, 0.12f, 0.07f, solid(rgb(0x3E2B22, 0.35f)), 0.3f);
        p.ellipse(0.30f, cy - ry * 0.05f, 0.08f, 0.05f, solid(rgb(0x3E2B22, 0.3f)), -0.2f);
        p.ellipse(0.48f, cy - ry * 0.45f, 0.07f, 0.04f, solid(rgb(0x3E2B22, 0.25f)), 0.1f);
    }

    // Sesame seeds, placed relative to the dome.
    const float seeds[][3] = {{0.33f, 0.55f, -0.6f}, {0.47f, 0.80f, 0.2f}, {0.62f, 0.62f, 0.8f}, {0.40f, 0.30f, 0.4f},
                              {0.56f, 0.36f, -0.3f}, {0.71f, 0.28f, 0.1f}, {0.25f, 0.22f, 1.0f}};
    const RGBA seed = soaked ? rgb(0xCFC4B4) : rgb(0xFFF3D2);
    const RGBA seedShade = soaked ? rgb(0x5A4A40, 0.6f) : rgb(0x8A4D14, 0.55f);
    for (const auto& s : seeds) {
        const float sx = s[0], sy = cy - ry * s[1];
        p.ellipse(sx + 0.006f, sy + 0.008f, 0.04f, 0.022f, solid(seedShade), s[2]);
        p.ellipse(sx, sy, 0.038f, 0.02f, solid(seed), s[2]);
    }

    // Shine. A soaked bun is glossy and wet.
    if (soaked) {
        p.ellipse(0.38f, cy - ry * 0.68f, 0.15f, 0.05f, solid(rgb(0xDDF5FF, 0.65f)), -0.3f);
        p.ellipse(0.70f, cy - ry * 0.40f, 0.05f, 0.025f, solid(rgb(0xDDF5FF, 0.55f)), 0.5f);
        // Beads of water on top and drips running off the edge.
        waterDrop(p, 0.56f, cy - ry * 0.55f, 0.03f);
        waterDrop(p, 0.28f, cy - ry * 0.40f, 0.025f);
        waterDrop(p, 0.20f, 0.79f, 0.032f);
        waterDrop(p, 0.52f, 0.84f, 0.036f);
        waterDrop(p, 0.81f, 0.78f, 0.03f);
    } else {
        p.ellipse(0.36f, cy - ry * 0.70f, 0.12f, 0.045f, solid(rgb(0xFFFFFF, 0.5f)), -0.35f);
    }
}

void hero(const Pen& p, int facing) {
    const float fx = float(kDirX[facing]), fy = float(kDirY[facing]);
    const float cx = 0.5f, cy = 0.62f, r = 0.29f, apex = 0.07f;

    p.ellipse(0.5f, 0.90f, 0.26f, 0.055f, solid(rgb(0x000000, 0.25f)));
    p.teardrop(cx, cy, r + 0.035f, apex - 0.045f, solid(rgb(0x0E3E73)));
    p.teardrop(cx, cy, r, apex, Radial{0.40f, 0.46f, 0.46f, rgb(0xCDF4FF), rgb(0x2B86DB)});
    // Darker crescent on the lower right gives the drop some volume.
    p.fill(cx - r, cy - r, cx + r, cy + r,
           [=](float x, float y) { return inCircle(x, y, cx, cy, r) && !inCircle(x, y, cx - 0.04f, cy - 0.05f, r); },
           solid(rgb(0x14509E, 0.35f)));
    p.ellipse(0.37f, 0.45f, 0.05f, 0.11f, solid(rgb(0xFFFFFF, 0.75f)), 0.45f);
    p.circle(0.445f, 0.29f, 0.022f, solid(rgb(0xFFFFFF, 0.8f)));

    // Eyes look where the drop is heading.
    const float ex = fx * 0.065f, ey = fy * 0.045f;
    for (int i = 0; i < 2; ++i) {
        const float x = (i ? 0.605f : 0.395f) + ex, y = 0.60f + ey;
        p.ellipse(x, y, 0.078f, 0.095f, solid(rgb(0x0E3E73)));
        p.ellipse(x, y, 0.062f, 0.079f, solid(rgb(0xFFFFFF)));
        const float px = x + fx * 0.022f, py = y + fy * 0.03f + 0.012f;
        p.circle(px, py, 0.037f, solid(rgb(0x10233F)));
        p.circle(px - 0.012f, py - 0.015f, 0.012f, solid(rgb(0xFFFFFF)));
    }
    if (facing != DirUp) {
        p.ellipse(0.29f + ex, 0.72f + ey, 0.05f, 0.03f, solid(rgb(0xFF7FA8, 0.5f)));
        p.ellipse(0.71f + ex, 0.72f + ey, 0.05f, 0.03f, solid(rgb(0xFF7FA8, 0.5f)));
        const float mx = 0.5f + ex * 1.1f, my = 0.715f + ey;
        p.fill(mx - 0.07f, my - 0.01f, mx + 0.07f, my + 0.06f,
               [=](float x, float y) { return y >= my && inEllipse(x, y, mx, my, 0.055f, 0.045f); },
               solid(rgb(0x0E3E73)));
    }
}

void icon(const Pen& p, bool background) {
    if (background) {
        p.roundRect(0.02f, 0.02f, 0.98f, 0.98f, 0.2f, solid(rgb(0x0E3E73)));
        p.roundRect(0.045f, 0.045f, 0.955f, 0.955f, 0.18f, VGrad{0.f, 1.f, rgb(0x9FE3FF), rgb(0x3A8FD8)});
    }
    // Puddle under the bun.
    p.ellipse(0.5f, 0.80f, 0.44f, 0.13f, solid(kWaterEdge));
    p.ellipse(0.5f, 0.80f, 0.41f, 0.11f, Radial{0.42f, 0.76f, 0.45f, kWaterLight, kWaterDeep});
    p.ring(0.5f, 0.81f, 0.33f, 0.07f, 0.015f, solid(rgb(0xFFFFFF, 0.4f)));

    bun(p.sub(0.13f, 0.10f, 0.74f), false);

    // Big drops falling onto it.
    waterDrop(p, 0.22f, 0.24f, 0.065f);
    waterDrop(p, 0.80f, 0.17f, 0.055f);
    waterDrop(p, 0.58f, 0.10f, 0.04f);
}

}  // namespace soak::art
