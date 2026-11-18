// Procedural Sokoban generation.
//
// 1. Carve a room: start from an open rectangle, stamp small wall shapes into
//    it, keep the largest connected area, fill dead ends, and break up large
//    open areas (which make puzzles trivial and blow up the search).
// 2. Place the puddles (goals) and put every bun on one: the solved state.
// 3. Breadth-first search *backwards* from the solved state using pulls (the
//    reverse of a push). Every state reached is solvable by construction, and
//    because BFS visits states in order of distance, the layer a state is
//    first found in is its exact minimum push count.
// 4. Pick the most demanding state found (longest solution, most changes of
//    bun / direction), preferring states with no bun already on a puddle.
//
// Several rooms are tried per level and the best puzzle wins. Higher levels
// use more buns, larger rooms, more attempts and a larger search budget.

#include "core/generator.h"

#include <algorithm>
#include <array>
#include <functional>
#include <thread>
#include <vector>

#include "core/keyset.h"
#include "core/rng.h"

namespace soak {
namespace {

constexpr int kStride = 16;  // working grid is 16x16 so a cell fits in a byte
constexpr int kCells = kStride * kStride;
constexpr int kOff[4] = {-kStride, 1, kStride, -1};
constexpr int kMaxBoxes = 7;  // 7 boxes + player = 8 bytes = one 64-bit key

using Grid = std::array<uint8_t, kCells>;

struct Shape {
    int n;
    int cells[4][2];
};

// Wall stamps. Single blocks are listed several times so they are picked more.
const Shape kShapes[] = {
    {1, {{0, 0}}},
    {1, {{0, 0}}},
    {1, {{0, 0}}},
    {2, {{0, 0}, {1, 0}}},
    {2, {{0, 0}, {0, 1}}},
    {3, {{0, 0}, {1, 0}, {2, 0}}},
    {3, {{0, 0}, {0, 1}, {0, 2}}},
    {3, {{0, 0}, {1, 0}, {0, 1}}},
    {3, {{0, 0}, {1, 0}, {1, 1}}},
    {3, {{1, 0}, {0, 1}, {1, 1}}},
    {3, {{0, 0}, {0, 1}, {1, 1}}},
    {4, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
    {4, {{0, 0}, {1, 0}, {2, 0}, {1, 1}}},
    {4, {{1, 0}, {0, 1}, {1, 1}, {2, 1}}},
    {4, {{0, 0}, {0, 1}, {0, 2}, {1, 1}}},
    {4, {{1, 0}, {1, 1}, {1, 2}, {0, 1}}},
};
constexpr int kShapeCount = int(sizeof(kShapes) / sizeof(kShapes[0]));

uint64_t encodeKey(const int* boxes, int k, int player) {
    uint64_t key = uint64_t(player);
    for (int i = 0; i < k; ++i) key |= uint64_t(boxes[i]) << (8 * (i + 1));
    return key;
}

void decodeKey(uint64_t key, int k, int* boxes, int& player) {
    player = int(key & 0xFF);
    for (int i = 0; i < k; ++i) boxes[i] = int((key >> (8 * (i + 1))) & 0xFF);
}

void sortSmall(int* a, int n) {
    for (int i = 1; i < n; ++i) {
        const int v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) {
            a[j + 1] = a[j];
            --j;
        }
        a[j + 1] = v;
    }
}

int countFloor(const Grid& floor) {
    int n = 0;
    for (uint8_t f : floor) n += f;
    return n;
}

void keepLargestRegion(Grid& floor) {
    std::array<int16_t, kCells> label;
    label.fill(-1);
    int stack[kCells];
    int16_t next = 0;
    int16_t best = -1;
    int bestSize = 0;
    for (int c = 0; c < kCells; ++c) {
        if (!floor[c] || label[c] >= 0) continue;
        int size = 0;
        int sp = 0;
        stack[sp++] = c;
        label[c] = next;
        while (sp) {
            const int v = stack[--sp];
            ++size;
            for (int d = 0; d < 4; ++d) {
                const int n = v + kOff[d];
                if (floor[n] && label[n] < 0) {
                    label[n] = next;
                    stack[sp++] = n;
                }
            }
        }
        if (size > bestSize) {
            bestSize = size;
            best = next;
        }
        ++next;
    }
    for (int c = 0; c < kCells; ++c) {
        if (floor[c] && label[c] != best) floor[c] = 0;
    }
}

// A floor cell with at most one open neighbour can never hold a useful bun.
void removeDeadEnds(Grid& floor) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (int c = 0; c < kCells; ++c) {
            if (!floor[c]) continue;
            int open = 0;
            for (int d = 0; d < 4; ++d) open += floor[c + kOff[d]] ? 1 : 0;
            if (open <= 1) {
                floor[c] = 0;
                changed = true;
            }
        }
    }
}

bool findOpenArea(const Grid& floor, int w, int h, int& ax, int& ay, int& aw, int& ah) {
    static const int kDims[2][2] = {{4, 3}, {3, 4}};
    for (int y = 1; y <= h; ++y) {
        for (int x = 1; x <= w; ++x) {
            for (const auto& dim : kDims) {
                const int bw = dim[0], bh = dim[1];
                if (x + bw - 1 > w || y + bh - 1 > h) continue;
                bool open = true;
                for (int yy = 0; yy < bh && open; ++yy) {
                    for (int xx = 0; xx < bw; ++xx) {
                        if (!floor[(y + yy) * kStride + x + xx]) {
                            open = false;
                            break;
                        }
                    }
                }
                if (open) {
                    ax = x;
                    ay = y;
                    aw = bw;
                    ah = bh;
                    return true;
                }
            }
        }
    }
    return false;
}

bool buildRoom(Rng& rng, const GenParams& p, Grid& floor) {
    const int w = rng.range(p.minW, p.maxW);
    const int h = rng.range(p.minH, p.maxH);
    floor.fill(0);
    for (int y = 1; y <= h; ++y)
        for (int x = 1; x <= w; ++x) floor[y * kStride + x] = 1;

    int target = w * h * p.wallPercent / 100;
    if (p.maxFloor > 0) target = std::max(target, w * h - p.maxFloor);
    int walls = 0;
    for (int tries = 0; tries < 400 && walls < target; ++tries) {
        const Shape& s = kShapes[rng.below(kShapeCount)];
        const int x0 = rng.range(1, w);
        const int y0 = rng.range(1, h);
        for (int i = 0; i < s.n; ++i) {
            const int x = x0 + s.cells[i][0];
            const int y = y0 + s.cells[i][1];
            if (x > w || y > h) continue;
            const int c = y * kStride + x;
            if (floor[c]) {
                floor[c] = 0;
                ++walls;
            }
        }
    }
    keepLargestRegion(floor);
    removeDeadEnds(floor);

    if (p.boxes >= 3) {
        int ax, ay, aw, ah;
        for (int i = 0; i < 40 && findOpenArea(floor, w, h, ax, ay, aw, ah); ++i) {
            const int x = ax + rng.range(1, aw - 2);
            const int y = ay + rng.range(1, ah - 2);
            floor[y * kStride + x] = 0;
            keepLargestRegion(floor);
            removeDeadEnds(floor);
        }
    }
    const int n = countFloor(floor);
    return n >= p.boxes * 3 + 6 && (p.maxFloor <= 0 || n <= p.maxFloor + 4);
}

std::vector<int> pickGoals(Rng& rng, const Grid& floor, int k) {
    std::vector<int> cells;
    for (int c = 0; c < kCells; ++c)
        if (floor[c]) cells.push_back(c);
    if (int(cells.size()) < k + 2) return {};
    for (int i = 0; i < k; ++i) {
        const int j = i + int(rng.below(uint32_t(cells.size()) - uint32_t(i)));
        std::swap(cells[size_t(i)], cells[size_t(j)]);
    }
    cells.resize(size_t(k));
    std::sort(cells.begin(), cells.end());
    return cells;
}

struct SearchResult {
    uint64_t key = 0;
    int dist = -1;  // exact minimum pushes from this state to the solution
    int lines = 0;  // straight push segments along that solution
    bool allOff = false;  // no bun starts on a puddle
    int score() const { return dist + lines; }
};

bool better(const SearchResult& a, const SearchResult& b) {
    if (a.dist < 0) return false;
    if (b.dist < 0) return true;
    if (a.allOff != b.allOff) return a.allOff;
    return a.score() > b.score();
}

struct Entry {
    uint64_t key;      // boxes + (non-canonical) player cell
    uint16_t lines;
    uint8_t lastBox;   // cell of the bun moved last (0 = none)
    uint8_t lastDir;
};

bool cancelled(const std::atomic<bool>* cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

SearchResult reverseSearch(const Grid& floor, const std::vector<int>& goals, int target,
                           int maxStates, const std::atomic<bool>* cancel, bool& aborted) {
    const int k = int(goals.size());
    std::array<uint8_t, kCells> isGoal{};
    for (int g : goals) isGoal[size_t(g)] = 1;

    std::array<uint32_t, kCells> boxMark{};
    std::array<uint32_t, kCells> reach{};
    uint32_t boxStamp = 0, reachStamp = 0;
    int stack[kCells];
    int boxes[kMaxBoxes];
    int moved[kMaxBoxes];

    std::vector<Entry> cur, next;

    // Solved state: every bun on a puddle, player in any of the free regions.
    for (int i = 0; i < k; ++i) boxes[i] = goals[size_t(i)];
    ++boxStamp;
    for (int i = 0; i < k; ++i) boxMark[size_t(boxes[i])] = boxStamp;
    ++reachStamp;
    for (int c = 0; c < kCells; ++c) {
        if (!floor[c] || boxMark[c] == boxStamp || reach[c] == reachStamp) continue;
        int sp = 0;
        stack[sp++] = c;
        reach[c] = reachStamp;
        while (sp) {
            const int v = stack[--sp];
            for (int d = 0; d < 4; ++d) {
                const int n = v + kOff[d];
                if (floor[n] && boxMark[n] != boxStamp && reach[n] != reachStamp) {
                    reach[n] = reachStamp;
                    stack[sp++] = n;
                }
            }
        }
        cur.push_back(Entry{encodeKey(boxes, k, c), 0, 0, 0});
    }

    KeySet visited(size_t(std::min(maxStates, 1 << 20)));
    SearchResult best;
    int explored = 0;

    for (int layer = 0; !cur.empty(); ++layer) {
        next.clear();
        for (const Entry& e : cur) {
            if ((explored & 1023) == 0 && cancelled(cancel)) {
                aborted = true;
                return best;
            }
            int player;
            decodeKey(e.key, k, boxes, player);
            ++boxStamp;
            for (int i = 0; i < k; ++i) boxMark[size_t(boxes[i])] = boxStamp;

            // Player's reachable region; its smallest cell is the canonical
            // player position so equivalent states share one key.
            ++reachStamp;
            int sp = 0, minCell = player;
            stack[sp++] = player;
            reach[size_t(player)] = reachStamp;
            while (sp) {
                const int v = stack[--sp];
                if (v < minCell) minCell = v;
                for (int d = 0; d < 4; ++d) {
                    const int n = v + kOff[d];
                    if (floor[n] && boxMark[n] != boxStamp && reach[n] != reachStamp) {
                        reach[n] = reachStamp;
                        stack[sp++] = n;
                    }
                }
            }
            const uint64_t ckey = encodeKey(boxes, k, minCell);
            if (!visited.insert(ckey)) continue;
            ++explored;

            bool allOff = true;
            for (int i = 0; i < k; ++i)
                if (isGoal[size_t(boxes[i])]) allOff = false;
            SearchResult r;
            r.key = ckey;
            r.dist = layer;
            r.lines = e.lines;
            r.allOff = allOff;
            if (better(r, best)) best = r;

            if (explored >= maxStates) return best;
            if (layer >= target) continue;

            // Pull: player stands at s next to bun b and steps back to t,
            // dragging the bun from b to s.
            for (int i = 0; i < k; ++i) {
                const int b = boxes[i];
                for (int d = 0; d < 4; ++d) {
                    const int s = b + kOff[d];
                    if (reach[size_t(s)] != reachStamp) continue;
                    const int t = s + kOff[d];
                    if (!floor[size_t(t)] || boxMark[size_t(t)] == boxStamp) continue;
                    for (int j = 0; j < k; ++j) moved[j] = boxes[j];
                    moved[i] = s;
                    sortSmall(moved, k);
                    const bool sameLine = e.lastBox == b && e.lastDir == d;
                    next.push_back(Entry{encodeKey(moved, k, t), uint16_t(e.lines + (sameLine ? 0 : 1)),
                                         uint8_t(s), uint8_t(d)});
                }
            }
        }
        std::swap(cur, next);
    }
    return best;
}

Level assemble(int number, const Grid& floor, const std::vector<int>& goals, const SearchResult& r,
               Rng& rng) {
    const int k = int(goals.size());
    int boxes[kMaxBoxes];
    int canon;
    decodeKey(r.key, k, boxes, canon);

    // Choose a start cell anywhere in the player's region.
    std::array<uint8_t, kCells> blocked{};
    for (int i = 0; i < k; ++i) blocked[size_t(boxes[i])] = 1;
    std::array<uint8_t, kCells> seen{};
    std::vector<int> region;
    std::vector<int> stack{canon};
    seen[size_t(canon)] = 1;
    while (!stack.empty()) {
        const int v = stack.back();
        stack.pop_back();
        region.push_back(v);
        for (int d = 0; d < 4; ++d) {
            const int n = v + kOff[d];
            if (floor[size_t(n)] && !blocked[size_t(n)] && !seen[size_t(n)]) {
                seen[size_t(n)] = 1;
                stack.push_back(n);
            }
        }
    }
    std::sort(region.begin(), region.end());
    const int player = region[rng.below(uint32_t(region.size()))];

    int minX = kStride, minY = kStride, maxX = -1, maxY = -1;
    for (int c = 0; c < kCells; ++c) {
        if (!floor[size_t(c)]) continue;
        minX = std::min(minX, c % kStride);
        maxX = std::max(maxX, c % kStride);
        minY = std::min(minY, c / kStride);
        maxY = std::max(maxY, c / kStride);
    }

    Level lv;
    lv.number = number;
    lv.width = maxX - minX + 3;
    lv.height = maxY - minY + 3;
    lv.tiles.assign(size_t(lv.cellCount()), Tile::Void);
    lv.goals.assign(size_t(lv.cellCount()), 0);

    auto isFloorAt = [&](int sx, int sy) {
        return sx >= 0 && sy >= 0 && sx < kStride && sy < kStride && floor[size_t(sy * kStride + sx)];
    };
    for (int y = 0; y < lv.height; ++y) {
        for (int x = 0; x < lv.width; ++x) {
            const int sx = x + minX - 1, sy = y + minY - 1;
            Tile t = Tile::Void;
            if (isFloorAt(sx, sy)) {
                t = Tile::Floor;
            } else {
                for (int dy = -1; dy <= 1 && t == Tile::Void; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (isFloorAt(sx + dx, sy + dy)) {
                            t = Tile::Wall;
                            break;
                        }
            }
            lv.tiles[size_t(y * lv.width + x)] = t;
        }
    }
    auto mapCell = [&](int c) { return (c / kStride - minY + 1) * lv.width + (c % kStride - minX + 1); };
    for (int g : goals) lv.goals[size_t(mapCell(g))] = 1;
    for (int i = 0; i < k; ++i) lv.boxes.push_back(mapCell(boxes[i]));
    lv.player = mapCell(player);
    lv.optimalPushes = r.dist;
    lv.boxLines = r.lines;
    return lv;
}

// Only used if every attempt fails, which should never happen in practice.
Level fallbackLevel(int number) {
    Level lv;
    lv.number = number;
    lv.width = 6;
    lv.height = 3;
    const char* row[3] = {"######", "#@$ .#", "######"};
    lv.tiles.assign(18, Tile::Wall);
    lv.goals.assign(18, 0);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 6; ++x) {
            const int c = y * 6 + x;
            const char ch = row[y][x];
            if (ch != '#') lv.tiles[size_t(c)] = Tile::Floor;
            if (ch == '@') lv.player = c;
            if (ch == '$') lv.boxes.push_back(c);
            if (ch == '.') lv.goals[size_t(c)] = 1;
        }
    }
    lv.optimalPushes = 2;
    lv.boxLines = 1;
    return lv;
}

uint64_t levelSeed(int number) {
    return 0x50AC7BE11ull ^ (uint64_t(number) * 0x9E3779B97F4A7C15ull);
}

struct Attempt {
    Grid floor{};
    std::vector<int> goals;
    SearchResult result;
    bool aborted = false;
};

// One room + search. Its random stream depends only on (level, index), so the
// outcome is the same no matter how attempts are spread over threads.
void runAttempt(int number, int index, const GenParams& p, const std::atomic<bool>* cancel, Attempt& out) {
    Rng rng(levelSeed(number) ^ (uint64_t(index + 1) * 0xD1B54A32D192ED03ull));
    bool built = false;
    for (int i = 0; i < 8 && !built; ++i) built = buildRoom(rng, p, out.floor);
    if (!built) return;
    out.goals = pickGoals(rng, out.floor, p.boxes);
    if (out.goals.empty()) return;
    out.result = reverseSearch(out.floor, out.goals, p.targetPushes, p.maxStates, cancel, out.aborted);
}

}  // namespace

GenParams paramsForLevel(int level) {
    const int L = std::clamp(level, 1, kMaxLevel);
    // Level at which the bun count becomes index+1. Beyond six buns the search
    // can no longer reach deep states quickly, so six is the ceiling.
    constexpr int kBands = 6;
    static const int kBoxStart[kBands] = {1, 2, 4, 9, 18, 36};
    // Base interior size for each bun count.
    static const int kW[kBands + 1] = {0, 5, 6, 7, 8, 9, 10};
    static const int kH[kBands + 1] = {0, 4, 5, 6, 7, 7, 8};

    GenParams p{};
    p.boxes = 1;
    for (int i = 1; i < kBands; ++i)
        if (L >= kBoxStart[i]) p.boxes = i + 1;
    const int k = p.boxes;

    // Rooms grow a little within each bun-count band.
    const int bandStart = kBoxStart[k - 1];
    const int bandLen = (k < kBands) ? kBoxStart[k] - bandStart : 30;
    const int grow = std::min(3, (L - bandStart) * 3 / std::max(1, bandLen));
    p.minW = kW[k];
    p.maxW = std::min(13, kW[k] + grow);
    p.minH = kH[k];
    p.maxH = std::min(11, kH[k] + grow);

    p.wallPercent = 18 + std::min(L, 40) / 5;
    // With many buns the search budget only reaches deep states when the
    // room has limited floor, so larger rooms get more walls (twistier
    // layouts) instead of more open space.
    p.maxFloor = k >= 5 ? 8 * k + 14 : 0;
    p.targetPushes = L < 30 ? 2 + 3 * L : 10000;
    p.minPushes = std::min(p.targetPushes, std::min(3 + 2 * L, 36));
    p.maxStates = k <= 2 ? 50000 : std::min(250000, 40000 + 6000 * L);
    p.attempts = std::min(12, 3 + L / 4);
    return p;
}

Level generateLevel(int level, const std::atomic<bool>* cancel) {
    const int number = std::clamp(level, 1, kMaxLevel);
    const GenParams p = paramsForLevel(number);
    // Constant (not tied to the worker count) so every machine builds the same level.
    const int hardLimit = p.attempts + 6;
    const int workers = int(std::clamp(std::thread::hardware_concurrency(), 1u, 6u));

    Attempt best;
    bool stop = false;
    for (int base = 0; base < hardLimit && !stop; base += workers) {
        if (cancelled(cancel)) return Level{};
        const int count = std::min(workers, hardLimit - base);
        std::vector<Attempt> batch(static_cast<size_t>(count));
        std::vector<std::thread> threads;
        for (int i = 1; i < count; ++i)
            threads.emplace_back(runAttempt, number, base + i, std::cref(p), cancel, std::ref(batch[size_t(i)]));
        runAttempt(number, base, p, cancel, batch[0]);
        for (std::thread& t : threads) t.join();

        // Merge strictly in attempt order so the result is thread-count independent.
        for (int i = 0; i < count; ++i) {
            Attempt& a = batch[size_t(i)];
            if (a.aborted) return Level{};
            if (better(a.result, best.result)) best = std::move(a);
            const bool good = best.result.dist >= p.minPushes && best.result.allOff;
            if (base + i + 1 >= p.attempts && good) {
                stop = true;
                break;
            }
        }
    }
    if (best.result.dist <= 0) return fallbackLevel(number);
    Rng rng(levelSeed(number));
    return assemble(number, best.floor, best.goals, best.result, rng);
}

}  // namespace soak
