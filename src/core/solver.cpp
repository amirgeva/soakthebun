#include "core/solver.h"

#include <algorithm>
#include <vector>

#include "core/keyset.h"

namespace soak {
namespace {

constexpr char kMoveChar[4] = {'U', 'R', 'D', 'L'};

// Insertion sort for the (at most 7) bun cells of a state.
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

// Cells from which a lone bun could still reach some puddle.
std::vector<uint8_t> aliveCells(const Level& lv, const int* off) {
    const int n = lv.cellCount();
    std::vector<uint8_t> alive(size_t(n), 0);
    std::vector<int> queue;
    for (int c = 0; c < n; ++c)
        if (lv.isGoal(c)) {
            alive[size_t(c)] = 1;
            queue.push_back(c);
        }
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        const int b = queue[qi];
        for (int d = 0; d < 4; ++d) {
            const int nb = b + off[d];
            if (!lv.isFloor(nb) || alive[size_t(nb)]) continue;
            if (!lv.isFloor(nb + off[d])) continue;
            alive[size_t(nb)] = 1;
            queue.push_back(nb);
        }
    }
    return alive;
}

// Shared machinery for push-level BFS: decoding states, the player's
// reachable region and its canonical (smallest) cell.
class PushSearch {
public:
    PushSearch(const Level& lv, int maxStates)
        : lv_(lv),
          k_(int(lv.boxes.size())),
          boxMark_(size_t(lv.cellCount()), 0),
          reach_(size_t(lv.cellCount()), 0),
          stack_(static_cast<size_t>(lv.cellCount())),
          visited_(size_t(std::min(maxStates, 1 << 21))) {
        for (int d = 0; d < 4; ++d) off[d] = lv.offset(d);
        alive = aliveCells(lv, off);
    }

    static bool supported(const Level& lv) {
        return lv.valid() && lv.cellCount() <= 256 && !lv.boxes.empty() && lv.boxes.size() <= 7;
    }

    uint64_t encode(const int* b, int player) const {
        uint64_t key = uint64_t(player);
        for (int i = 0; i < k_; ++i) key |= uint64_t(b[i]) << (8 * (i + 1));
        return key;
    }

    uint64_t startKey() {
        for (int i = 0; i < k_; ++i) boxes[i] = lv_.boxes[size_t(i)];
        sortSmall(boxes, k_);
        return encode(boxes, lv_.player);
    }

    // Loads a raw state; returns its canonical key, or 0 if already visited.
    uint64_t visit(uint64_t key) {
        const int player = int(key & 0xFF);
        for (int i = 0; i < k_; ++i) boxes[i] = int((key >> (8 * (i + 1))) & 0xFF);
        ++boxStamp_;
        for (int i = 0; i < k_; ++i) boxMark_[size_t(boxes[i])] = boxStamp_;
        ++reachStamp_;
        int sp = 0, minCell = player;
        stack_[size_t(sp++)] = player;
        reach_[size_t(player)] = reachStamp_;
        while (sp) {
            const int v = stack_[size_t(--sp)];
            minCell = std::min(minCell, v);
            for (int d = 0; d < 4; ++d) {
                const int c = v + off[d];
                if (lv_.isFloor(c) && boxMark_[size_t(c)] != boxStamp_ && reach_[size_t(c)] != reachStamp_) {
                    reach_[size_t(c)] = reachStamp_;
                    stack_[size_t(sp++)] = c;
                }
            }
        }
        const uint64_t ckey = encode(boxes, minCell);
        return visited_.insert(ckey) ? ckey : 0;
    }

    bool solved() const {
        for (int i = 0; i < k_; ++i)
            if (!lv_.isGoal(boxes[i])) return false;
        return true;
    }

    // Calls fn(rawKey, boxCell, dir) for every legal, non-dead push from the
    // state loaded by the last visit().
    template <class Fn>
    void forEachPush(Fn fn) const {
        int moved[7];
        for (int i = 0; i < k_; ++i) {
            const int b = boxes[i];
            for (int d = 0; d < 4; ++d) {
                const int from = b - off[d];
                const int to = b + off[d];
                if (reach_[size_t(from)] != reachStamp_) continue;
                if (!lv_.isFloor(to) || boxMark_[size_t(to)] == boxStamp_ || !alive[size_t(to)]) continue;
                for (int j = 0; j < k_; ++j) moved[j] = boxes[j];
                moved[i] = to;
                sortSmall(moved, k_);
                fn(encode(moved, b), b, d);
            }
        }
    }

    int off[4];
    std::vector<uint8_t> alive;
    int boxes[7];

private:
    const Level& lv_;
    int k_;
    std::vector<uint32_t> boxMark_, reach_;
    uint32_t boxStamp_ = 0, reachStamp_ = 0;
    std::vector<int> stack_;
    KeySet visited_;
};

// Shortest walk from `from` to `to` avoiding buns, appended as move letters.
bool appendWalk(const Level& lv, const std::vector<uint8_t>& box, int from, int to, std::string& moves) {
    if (from == to) return true;
    const int n = lv.cellCount();
    std::vector<int8_t> via(size_t(n), -1);
    std::vector<int> queue{from};
    via[size_t(from)] = 4;
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        const int v = queue[qi];
        if (v == to) break;
        for (int d = 0; d < 4; ++d) {
            const int c = v + lv.offset(d);
            if (!lv.isFloor(c) || box[size_t(c)] || via[size_t(c)] >= 0) continue;
            via[size_t(c)] = int8_t(d);
            queue.push_back(c);
        }
    }
    if (via[size_t(to)] < 0) return false;
    std::string path;
    for (int c = to; c != from;) {
        const int d = via[size_t(c)];
        path += kMoveChar[d];
        c -= lv.offset(d);
    }
    moves.append(path.rbegin(), path.rend());
    return true;
}

}  // namespace

SolveResult solveLevel(const Level& lv, int maxStates) {
    SolveResult res;
    if (!PushSearch::supported(lv)) return res;
    PushSearch search(lv, maxStates);
    std::vector<uint64_t> cur{search.startKey()}, next;

    for (int layer = 0; !cur.empty(); ++layer) {
        next.clear();
        for (uint64_t key : cur) {
            if (!search.visit(key)) continue;
            ++res.explored;
            if (search.solved()) {
                res.solved = true;
                res.pushes = layer;
                return res;
            }
            if (res.explored >= maxStates) return res;
            search.forEachPush([&](uint64_t raw, int, int) { next.push_back(raw); });
        }
        std::swap(cur, next);
    }
    res.exhausted = true;
    return res;
}

std::string solutionMoves(const Level& lv, int maxStates) {
    if (!PushSearch::supported(lv)) return {};
    PushSearch search(lv, maxStates);

    struct Entry {
        uint64_t raw;
        int parent;  // index into `pushes` of the state this came from
        int16_t box;
        int8_t dir;
    };
    struct Push {
        int parent;
        int16_t box;
        int8_t dir;
    };
    std::vector<Push> pushes;  // one per visited state
    std::vector<Entry> cur{Entry{search.startKey(), -1, -1, -1}}, next;
    int goal = -1;

    for (; !cur.empty() && goal < 0; std::swap(cur, next)) {
        next.clear();
        for (const Entry& e : cur) {
            if (!search.visit(e.raw)) continue;
            const int index = int(pushes.size());
            pushes.push_back(Push{e.parent, e.box, e.dir});
            if (search.solved()) {
                goal = index;
                break;
            }
            if (index + 1 >= maxStates) return {};
            search.forEachPush([&](uint64_t raw, int b, int d) {
                next.push_back(Entry{raw, index, int16_t(b), int8_t(d)});
            });
        }
    }
    if (goal < 0) return {};

    std::vector<Push> path;
    for (int i = goal; pushes[size_t(i)].parent >= 0; i = pushes[size_t(i)].parent) path.push_back(pushes[size_t(i)]);
    std::reverse(path.begin(), path.end());

    // Replay the pushes, walking the player to each one.
    std::vector<uint8_t> box(size_t(lv.cellCount()), 0);
    for (int b : lv.boxes) box[size_t(b)] = 1;
    int player = lv.player;
    std::string moves;
    for (const Push& p : path) {
        const int off = lv.offset(p.dir);
        if (!appendWalk(lv, box, player, p.box - off, moves)) return {};
        moves += kMoveChar[p.dir];
        box[size_t(p.box)] = 0;
        box[size_t(p.box + off)] = 1;
        player = p.box;
    }
    return moves;
}

}  // namespace soak
