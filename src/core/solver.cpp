#include "core/solver.h"

#include <algorithm>
#include <vector>

#include "core/keyset.h"

namespace soak {

SolveResult solveLevel(const Level& lv, int maxStates) {
    SolveResult res;
    const int n = lv.cellCount();
    const int k = int(lv.boxes.size());
    if (!lv.valid() || n > 256 || k < 1 || k > 7) return res;

    int off[4];
    for (int d = 0; d < 4; ++d) off[d] = lv.offset(d);

    // Cells from which a lone bun could still reach some puddle.
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

    auto encode = [k](const int* boxes, int player) {
        uint64_t key = uint64_t(player);
        for (int i = 0; i < k; ++i) key |= uint64_t(boxes[i]) << (8 * (i + 1));
        return key;
    };

    std::vector<uint32_t> boxMark(size_t(n), 0), reach(size_t(n), 0);
    uint32_t boxStamp = 0, reachStamp = 0;
    std::vector<int> stack(static_cast<size_t>(n));
    int boxes[7], moved[7];

    for (int i = 0; i < k; ++i) boxes[i] = lv.boxes[size_t(i)];
    std::sort(boxes, boxes + k);
    std::vector<uint64_t> cur{encode(boxes, lv.player)}, next;
    KeySet visited(size_t(std::min(maxStates, 1 << 21)));

    for (int layer = 0; !cur.empty(); ++layer) {
        next.clear();
        for (uint64_t key : cur) {
            const int player = int(key & 0xFF);
            for (int i = 0; i < k; ++i) boxes[i] = int((key >> (8 * (i + 1))) & 0xFF);
            ++boxStamp;
            for (int i = 0; i < k; ++i) boxMark[size_t(boxes[i])] = boxStamp;
            ++reachStamp;
            int sp = 0, minCell = player;
            stack[size_t(sp++)] = player;
            reach[size_t(player)] = reachStamp;
            while (sp) {
                const int v = stack[size_t(--sp)];
                minCell = std::min(minCell, v);
                for (int d = 0; d < 4; ++d) {
                    const int c = v + off[d];
                    if (lv.isFloor(c) && boxMark[size_t(c)] != boxStamp && reach[size_t(c)] != reachStamp) {
                        reach[size_t(c)] = reachStamp;
                        stack[size_t(sp++)] = c;
                    }
                }
            }
            if (!visited.insert(encode(boxes, minCell))) continue;
            ++res.explored;

            bool done = true;
            for (int i = 0; i < k; ++i)
                if (!lv.isGoal(boxes[i])) done = false;
            if (done) {
                res.solved = true;
                res.pushes = layer;
                return res;
            }
            if (res.explored >= maxStates) return res;

            for (int i = 0; i < k; ++i) {
                const int b = boxes[i];
                for (int d = 0; d < 4; ++d) {
                    const int from = b - off[d];
                    const int to = b + off[d];
                    if (reach[size_t(from)] != reachStamp) continue;
                    if (!lv.isFloor(to) || boxMark[size_t(to)] == boxStamp || !alive[size_t(to)]) continue;
                    for (int j = 0; j < k; ++j) moved[j] = boxes[j];
                    moved[i] = to;
                    std::sort(moved, moved + k);
                    next.push_back(encode(moved, b));
                }
            }
        }
        std::swap(cur, next);
    }
    res.exhausted = true;
    return res;
}

}  // namespace soak
