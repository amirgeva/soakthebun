// Writes the procedurally drawn app icon as a multi-resolution Windows .ico
// (32-bit BMP entries). Run at build time so the .exe gets the same icon the
// game sets on its window at runtime.
//
// Usage: soak_icongen output.ico

#include <cstdint>
#include <cstdio>
#include <vector>

#include "gfx/art.h"
#include "gfx/canvas.h"

namespace {

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

std::vector<uint8_t> dibImage(int size) {
    soak::Canvas c(size, size);
    soak::art::icon(soak::Pen(c), true);
    const std::vector<uint8_t> rgba = c.toRGBA8();

    std::vector<uint8_t> b;
    const uint32_t maskRow = uint32_t(((size + 31) / 32) * 4);
    put32(b, 40);                    // BITMAPINFOHEADER size
    put32(b, uint32_t(size));        // width
    put32(b, uint32_t(size * 2));    // height (image + mask)
    put16(b, 1);                     // planes
    put16(b, 32);                    // bits per pixel
    put32(b, 0);                     // BI_RGB
    put32(b, uint32_t(size * size * 4) + maskRow * uint32_t(size));
    put32(b, 0);
    put32(b, 0);
    put32(b, 0);
    put32(b, 0);
    for (int y = size - 1; y >= 0; --y) {  // bottom-up BGRA
        for (int x = 0; x < size; ++x) {
            const uint8_t* p = &rgba[size_t((y * size + x) * 4)];
            b.push_back(p[2]);
            b.push_back(p[1]);
            b.push_back(p[0]);
            b.push_back(p[3]);
        }
    }
    b.insert(b.end(), size_t(maskRow) * size_t(size), 0);  // AND mask unused with alpha
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s output.ico\n", argv[0]);
        return 2;
    }
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    const int count = int(sizeof(sizes) / sizeof(sizes[0]));
    std::vector<std::vector<uint8_t>> images;
    for (int s : sizes) images.push_back(dibImage(s));

    std::vector<uint8_t> out;
    put16(out, 0);
    put16(out, 1);  // icon
    put16(out, uint16_t(count));
    uint32_t offset = 6 + 16 * uint32_t(count);
    for (int i = 0; i < count; ++i) {
        out.push_back(uint8_t(sizes[i] >= 256 ? 0 : sizes[i]));
        out.push_back(uint8_t(sizes[i] >= 256 ? 0 : sizes[i]));
        out.push_back(0);  // palette
        out.push_back(0);  // reserved
        put16(out, 1);     // planes
        put16(out, 32);    // bpp
        put32(out, uint32_t(images[size_t(i)].size()));
        put32(out, offset);
        offset += uint32_t(images[size_t(i)].size());
    }
    for (const auto& img : images) out.insert(out.end(), img.begin(), img.end());

    FILE* f = std::fopen(argv[1], "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok ? 0 : 1;
}
