#pragma once

#include "gfx/canvas.h"

// Procedural art. Each function draws into the unit square of the given pen.
namespace soak::art {

void floorTile(const Pen& p, int variant);  // kitchen tile, variant 0/1 for a checkerboard
void wallTile(const Pen& p);                // stone bricks that tile seamlessly
void puddle(const Pen& p);                  // goal: a water puddle
void ripple(const Pen& p);                  // white ring, animated over puddles
void bun(const Pen& p, bool soaked);        // the "box": a sesame bun, dry or soggy
void hero(const Pen& p, int facing);        // the player: a little water drop
void dot(const Pen& p);                     // soft white particle
void arrow(const Pen& p);                   // hint arrow, pointing right
void icon(const Pen& p, bool background);   // app icon / title art

}  // namespace soak::art
