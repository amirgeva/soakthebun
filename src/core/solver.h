#pragma once

#include <atomic>
#include <string>

#include "core/level.h"

namespace soak {

struct SolveResult {
    bool solved = false;
    bool exhausted = false;  // proven: no solution exists from this position
    bool cancelled = false;
    int pushes = -1;         // minimum number of pushes when solved
    int explored = 0;
    std::string moves;       // solution as 'U'/'D'/'L'/'R' (solveWithMoves only)
};

// Breadth-first search over pushes (forward play) from the level's bun and
// player positions. Independent of the generator, so it can verify generated
// levels. Gives up (solved and exhausted both false) after maxStates.
SolveResult solveLevel(const Level& level, int maxStates);

// Like solveLevel, but also returns the full solution as player moves:
// minimum pushes, with the shortest walk between pushes. Works from any
// position, so it also powers in-game hints. Stops early if `cancel` is set.
SolveResult solveWithMoves(const Level& level, int maxStates, const std::atomic<bool>* cancel = nullptr);

// Convenience: just the moves, or empty if no solution was found.
std::string solutionMoves(const Level& level, int maxStates);

}  // namespace soak
