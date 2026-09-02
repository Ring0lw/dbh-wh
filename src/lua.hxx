#pragma once

#include <string>

namespace lua {

struct set {
    bool boxes = true;
    bool skel = true;
    bool labels = true;
    bool player = true;
    int alive = 0;
    std::string note;
};

void tick() noexcept;
const set& cur() noexcept;

}
