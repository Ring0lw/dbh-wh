#pragma once

namespace cfg {

struct set {
    bool boxes = true;
    bool skel = true;
    bool labels = true;
    bool player = true;
    bool player_all_bones = false;
    float label_dist = 60.0f;
    float skel_dist = 40.0f;
    float box_dist = 150.0f;
};

inline set& cur() noexcept {
    static set s;
    return s;
}

}
