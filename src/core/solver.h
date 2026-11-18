#pragma once

#include "core/level.h"

namespace soak {

struct SolveResult {
    bool solved = false;
    bool exhausted = false;  // whole state space searched without a solution
    int pushes = -1;         // minimum number of pushes when solved
    int explored = 0;
};

// Breadth-first search over pushes (forward play). Independent of the
// generator, so it can verify generated levels. Gives up after maxStates.
SolveResult solveLevel(const Level& level, int maxStates);

}  // namespace soak
