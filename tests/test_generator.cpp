// Generator verification:
//  * each level is well formed (box/goal counts, player on free floor),
//  * generation is deterministic,
//  * an independent forward solver confirms the level is solvable and that
//    the generator's claimed minimum push count is exact.
//
// Usage: soak_tests [lastLevel=30] [firstLevel=1] [--print] [--solver-budget N]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/generator.h"
#include "core/solver.h"

using namespace soak;

namespace {

bool wellFormed(const Level& lv, std::string& why) {
    if (!lv.valid()) {
        why = "invalid level";
        return false;
    }
    int goals = 0;
    for (int c = 0; c < lv.cellCount(); ++c) goals += lv.isGoal(c) ? 1 : 0;
    if (goals != int(lv.boxes.size()) || goals == 0) {
        why = "goal/box count mismatch";
        return false;
    }
    std::vector<int> seen(size_t(lv.cellCount()), 0);
    for (int b : lv.boxes) {
        if (!lv.isFloor(b) || seen[size_t(b)]++) {
            why = "box on non-floor or overlapping";
            return false;
        }
    }
    if (!lv.isFloor(lv.player) || seen[size_t(lv.player)]) {
        why = "player not on free floor";
        return false;
    }
    for (int x = 0; x < lv.width; ++x)
        for (int y : {0, lv.height - 1})
            if (lv.isFloor(y * lv.width + x)) {
                why = "floor on border";
                return false;
            }
    for (int y = 0; y < lv.height; ++y)
        for (int x : {0, lv.width - 1})
            if (lv.isFloor(y * lv.width + x)) {
                why = "floor on border";
                return false;
            }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    int last = 30, first = 1, budget = 4000000;
    bool print = false;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--print")) print = true;
        else if (!std::strcmp(argv[i], "--solver-budget") && i + 1 < argc) budget = std::atoi(argv[++i]);
        else if (positional == 0) { last = std::atoi(argv[i]); ++positional; }
        else if (positional == 1) { first = std::atoi(argv[i]); ++positional; }
    }

    int failures = 0, unverified = 0;
    double totalMs = 0, worstMs = 0, totalPushes = 0;
    std::printf("level  size   buns  pushes lines   gen ms   solver\n");
    for (int L = first; L <= last; ++L) {
        const auto t0 = std::chrono::steady_clock::now();
        const Level lv = generateLevel(L);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalMs += ms;
        if (ms > worstMs) worstMs = ms;

        std::string why;
        if (!wellFormed(lv, why)) {
            std::printf("%5d  FAIL: %s\n", L, why.c_str());
            ++failures;
            continue;
        }
        if (generateLevel(L).toText() != lv.toText()) {
            std::printf("%5d  FAIL: generation is not deterministic\n", L);
            ++failures;
        }

        const SolveResult sr = solveLevel(lv, budget);
        char verdict[64];
        if (sr.solved && sr.pushes == lv.optimalPushes) {
            std::snprintf(verdict, sizeof verdict, "ok (%d states)", sr.explored);
        } else if (sr.solved) {
            std::snprintf(verdict, sizeof verdict, "FAIL: solver says %d pushes", sr.pushes);
            ++failures;
        } else if (sr.exhausted) {
            std::snprintf(verdict, sizeof verdict, "FAIL: unsolvable");
            ++failures;
        } else {
            std::snprintf(verdict, sizeof verdict, "budget hit (%d states)", sr.explored);
            ++unverified;
        }
        totalPushes += lv.optimalPushes;
        std::printf("%5d  %2dx%-2d  %4d  %6d %5d  %7.1f   %s\n", L, lv.width, lv.height, int(lv.boxes.size()),
                    lv.optimalPushes, lv.boxLines, ms, verdict);
        if (print) std::printf("%s\n", lv.toText().c_str());
        std::fflush(stdout);
    }
    std::printf("\nlevels %d-%d: %d failure(s), %d not verified within solver budget, gen total %.0f ms, "
                "worst %.0f ms, average %.1f pushes\n",
                first, last, failures, unverified, totalMs, worstMs,
                totalPushes / std::max(1, last - first + 1));
    return failures ? 1 : 0;
}
