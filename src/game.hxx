#pragma once

#include <cstdint>
#include <vector>

namespace game {

struct vec3 {
    float x, y, z;
};

struct seg {
    vec3 a, b;
};

struct ent {
    vec3 pos;
    float bot;
    float top;
    float hw;
    bool has_head;
    bool player;
    std::uint32_t cls;
    char name[40];
    std::vector<seg> bones;
};

struct cam {
    float m[16];
    float vfov;
    float aspect;
    bool ok;
};

struct snap {
    cam c{};
    std::vector<ent> ents;
    std::uint32_t seen = 0;
    bool locked = false;
};

bool init() noexcept;
void read(snap& s) noexcept;

}
