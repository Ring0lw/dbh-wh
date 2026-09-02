#pragma once

#include "cfg.hxx"
#include "game.hxx"

struct ImDrawList;

namespace esp {

int draw(ImDrawList* dl, const game::snap& s, float w, float h, const cfg::set& cfg) noexcept;

}
