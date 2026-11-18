#pragma once

namespace soak {

// Progress kept between sessions, stored in the per-user preferences folder
// that SDL picks for each platform (e.g. %APPDATA%\SoakTheBun\SoakTheBun on
// Windows, ~/.local/share/SoakTheBun/SoakTheBun on Linux,
// ~/Library/Application Support/SoakTheBun/SoakTheBun on macOS).
struct SaveData {
    int lastLevel = 1;  // level to offer by default in the menu
    int bestLevel = 0;  // highest level completed
    bool muted = false;
};

SaveData loadSave();
void storeSave(const SaveData& data);

}  // namespace soak
