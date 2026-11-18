#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/game.h"

namespace {

void usage() {
    std::printf(
        "Soak the Bun - push every bun into a puddle.\n\n"
        "  --level N              preselect level N in the menu\n"
        "  --size WxH             window size (default 1024x768)\n"
        "  --screenshot FILE.bmp  render one frame and exit\n"
        "  --scene menu|game|won|next  scene for --screenshot (default game)\n"
        "  --moves UDLR...        moves to play before the screenshot\n");
}

}  // namespace

int main(int argc, char* argv[]) {
    soak::Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        if (a == "--level" && next) {
            opt.level = std::atoi(next);
            ++i;
        } else if (a == "--size" && next) {
            int w = 0, h = 0;
            if (std::sscanf(next, "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240) {
                opt.width = w;
                opt.height = h;
            }
            ++i;
        } else if (a == "--screenshot" && next) {
            opt.screenshot = next;
            ++i;
        } else if (a == "--scene" && next) {
            opt.scene = next;
            ++i;
        } else if (a == "--moves" && next) {
            opt.moves = next;
            ++i;
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        }
    }
    soak::Game game;
    return game.run(opt);
}
