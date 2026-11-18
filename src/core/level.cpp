#include "core/level.h"

namespace soak {

std::string Level::toText() const {
    std::vector<uint8_t> box(size_t(cellCount()), 0);
    for (int b : boxes) box[size_t(b)] = 1;
    std::string out;
    out.reserve(size_t((width + 1) * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int c = y * width + x;
            char ch = ' ';
            if (tiles[size_t(c)] == Tile::Wall) {
                ch = '#';
            } else if (tiles[size_t(c)] == Tile::Floor) {
                const bool g = isGoal(c);
                if (box[size_t(c)]) ch = g ? '*' : '$';
                else if (c == player) ch = g ? '+' : '@';
                else ch = g ? '.' : ' ';
            }
            out += ch;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        out += '\n';
    }
    return out;
}

void Board::load(const Level& level) {
    level_ = level;
    restart();
}

void Board::restart() {
    player_ = level_.player;
    boxes_ = level_.boxes;
    boxAt_.assign(size_t(level_.cellCount()), -1);
    for (size_t i = 0; i < boxes_.size(); ++i) boxAt_[size_t(boxes_[i])] = int16_t(i);
    history_.clear();
    moves_ = 0;
    pushes_ = 0;
}

Board::Result Board::move(int dir) {
    if (!level_.valid()) return Result::Blocked;
    const int off = level_.offset(dir);
    const int target = player_ + off;
    if (!level_.isFloor(target)) return Result::Blocked;

    const int16_t box = boxAt_[size_t(target)];
    if (box >= 0) {
        const int beyond = target + off;
        if (!level_.isFloor(beyond) || boxAt_[size_t(beyond)] >= 0) return Result::Blocked;
        boxAt_[size_t(target)] = -1;
        boxAt_[size_t(beyond)] = box;
        boxes_[size_t(box)] = beyond;
        ++pushes_;
    }
    player_ = target;
    ++moves_;
    history_.push_back(Step{int8_t(dir), box});
    return box >= 0 ? Result::Pushed : Result::Moved;
}

bool Board::undo() {
    if (history_.empty()) return false;
    const Step step = history_.back();
    history_.pop_back();
    const int off = level_.offset(step.dir);
    if (step.box >= 0) {
        const int from = player_ + off;
        boxAt_[size_t(from)] = -1;
        boxAt_[size_t(player_)] = step.box;
        boxes_[size_t(step.box)] = player_;
        --pushes_;
    }
    player_ -= off;
    --moves_;
    return true;
}

bool Board::solved() const {
    return !boxes_.empty() && boxesOnGoals() == int(boxes_.size());
}

int Board::boxesOnGoals() const {
    int n = 0;
    for (int b : boxes_) n += level_.isGoal(b) ? 1 : 0;
    return n;
}

}  // namespace soak
