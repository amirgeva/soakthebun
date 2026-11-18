#include "app/save.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/generator.h"

namespace soak {
namespace {

std::string savePath() {
    char* base = SDL_GetPrefPath("SoakTheBun", "SoakTheBun");
    if (!base) return {};
    std::string path = std::string(base) + "progress.ini";
    SDL_free(base);
    return path;
}

}  // namespace

SaveData loadSave() {
    SaveData data;
    const std::string path = savePath();
    if (path.empty()) return data;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return data;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const int value = std::atoi(eq + 1);
        if (!std::strcmp(line, "last_level")) data.lastLevel = value;
        else if (!std::strcmp(line, "best_level")) data.bestLevel = value;
        else if (!std::strcmp(line, "muted")) data.muted = value != 0;
    }
    std::fclose(f);
    data.lastLevel = std::clamp(data.lastLevel, 1, kMaxLevel);
    data.bestLevel = std::clamp(data.bestLevel, 0, kMaxLevel);
    return data;
}

void storeSave(const SaveData& data) {
    const std::string path = savePath();
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        SDL_Log("Could not write %s", path.c_str());
        return;
    }
    std::fprintf(f, "last_level=%d\nbest_level=%d\nmuted=%d\n", data.lastLevel, data.bestLevel, data.muted ? 1 : 0);
    std::fclose(f);
}

}  // namespace soak
