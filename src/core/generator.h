#pragma once

#include <atomic>

#include "core/level.h"

namespace soak {

constexpr int kMaxLevel = 999;

// Knobs that the difficulty curve turns as the level number grows.
struct GenParams {
    int boxes;         // number of buns (and puddles)
    int minW, maxW;    // interior room width range
    int minH, maxH;    // interior room height range
    int wallPercent;   // obstacle density stamped into the room
    int maxFloor;      // if > 0, add walls until about this much floor is left
    int targetPushes;  // deepest solution length considered
    int minPushes;     // keep trying rooms until a puzzle at least this long appears
    int maxStates;     // search budget per room
    int attempts;      // rooms tried per level (best one wins)
};

GenParams paramsForLevel(int level);

// Generates the puzzle for a level number. Deterministic: the same number
// always produces the same puzzle on every platform. Every generated puzzle is
// solvable by construction, and Level::optimalPushes is the exact minimum.
// Returns an invalid Level if `cancel` becomes true while generating.
Level generateLevel(int level, const std::atomic<bool>* cancel = nullptr);

}  // namespace soak
