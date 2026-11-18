#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace soak {

enum class Tile : uint8_t { Void = 0, Wall = 1, Floor = 2 };

enum Dir : int { DirUp = 0, DirRight = 1, DirDown = 2, DirLeft = 3 };
constexpr int kDirX[4] = {0, 1, 0, -1};
constexpr int kDirY[4] = {-1, 0, 1, 0};

// A static puzzle description. Cells are indexed row-major (y * width + x).
// Every floor cell is surrounded by at least one ring of non-floor cells, so
// stepping one cell from a floor cell never leaves the grid.
struct Level {
    int number = 0;
    int width = 0;
    int height = 0;
    std::vector<Tile> tiles;
    std::vector<uint8_t> goals;  // 1 = water puddle
    std::vector<int> boxes;      // bun cells
    int player = -1;
    int optimalPushes = 0;  // exact minimum number of pushes to solve
    int boxLines = 0;       // straight push segments in that solution

    bool valid() const { return width > 0 && height > 0 && player >= 0; }
    int cellCount() const { return width * height; }
    bool isFloor(int cell) const { return tiles[size_t(cell)] == Tile::Floor; }
    bool isGoal(int cell) const { return goals[size_t(cell)] != 0; }
    int offset(int dir) const { return kDirX[dir] + kDirY[dir] * width; }

    // Standard Sokoban text (XSB) form: # wall, . goal, $ box, * box on goal,
    // @ player, + player on goal.
    std::string toText() const;
};

// Mutable play state for one level, with full undo.
class Board {
public:
    enum class Result { Blocked, Moved, Pushed };

    void load(const Level& level);
    void restart();
    Result move(int dir);
    bool undo();

    bool solved() const;
    int boxesOnGoals() const;

    const Level& level() const { return level_; }
    int player() const { return player_; }
    const std::vector<int>& boxes() const { return boxes_; }
    int boxAt(int cell) const { return boxAt_[size_t(cell)]; }
    int moves() const { return moves_; }
    int pushes() const { return pushes_; }
    // Index of the bun moved by the most recent move, or -1.
    int lastPushedBox() const { return history_.empty() ? -1 : history_.back().box; }

private:
    struct Step {
        int8_t dir;
        int16_t box;
    };

    Level level_;
    int player_ = 0;
    std::vector<int> boxes_;
    std::vector<int16_t> boxAt_;
    std::vector<Step> history_;
    int moves_ = 0;
    int pushes_ = 0;
};

}  // namespace soak
