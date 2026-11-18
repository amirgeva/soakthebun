#include "gfx/canvas.h"

namespace soak {

std::vector<uint8_t> Canvas::toRGBA8() const {
    std::vector<uint8_t> out(px_.size() * 4);
    for (size_t i = 0; i < px_.size(); ++i) {
        const RGBA& p = px_[i];
        uint8_t* o = &out[i * 4];
        if (p.a <= 0.f) {
            o[0] = o[1] = o[2] = o[3] = 0;
            continue;
        }
        const float inv = 1.f / p.a;
        o[0] = uint8_t(std::lround(clamp01(p.r * inv) * 255.f));
        o[1] = uint8_t(std::lround(clamp01(p.g * inv) * 255.f));
        o[2] = uint8_t(std::lround(clamp01(p.b * inv) * 255.f));
        o[3] = uint8_t(std::lround(clamp01(p.a) * 255.f));
    }
    return out;
}

}  // namespace soak
