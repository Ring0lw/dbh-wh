#include "esp.hxx"

#include "lg.hxx"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace esp {
namespace {

int fwd_sign = 1;
int behind_streak = 0;
unsigned frames = 0;

struct pt {
    float x, y;
};

struct basis {
    game::vec3 right, up, fwd, pos;
};

game::vec3 row(const float* m, int i) noexcept {
    game::vec3 v{m[4 * i], m[4 * i + 1], m[4 * i + 2]};
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 1e-6f) {
        v.x /= len;
        v.y /= len;
        v.z /= len;
    }
    return v;
}

float dot(const game::vec3& a, const game::vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

bool w2s(const basis& b, float vfov, const game::vec3& p, float w, float h, pt& out, float& depth) noexcept {
    game::vec3 d{p.x - b.pos.x, p.y - b.pos.y, p.z - b.pos.z};
    float x = dot(d, b.right);
    float y = dot(d, b.up);
    float z = dot(d, b.fwd) * static_cast<float>(fwd_sign);
    depth = z;
    if (z < 0.05f) return false;
    float f = 1.0f / std::tan(vfov * 0.5f);
    float sx = x * f * (h / w) / z;
    float sy = y * f / z;
    out.x = (sx * 0.5f + 0.5f) * w;
    out.y = (0.5f - sy * 0.5f) * h;
    return true;
}

}

int draw(ImDrawList* dl, const game::snap& s, float w, float h, const cfg::set& cfg) noexcept {
    ++frames;
    int drawn = 0;
    if (!dl || !s.c.ok || w <= 0 || h <= 0) return 0;
    if (!cfg.boxes && !cfg.skel && !cfg.labels) return 0;

    basis b{row(s.c.m, 0), row(s.c.m, 1), row(s.c.m, 2), {s.c.m[12], s.c.m[13], s.c.m[14]}};
    ImFont* font = ImGui::GetFont();
    const float fs = std::max(14.0f, h / 60.0f);

    int front = 0;
    int behind = 0;
    for (const auto& e : s.ents) {
        pt foot, head, side;
        float dz, dh, ds;
        game::vec3 lo{e.pos.x, e.bot, e.pos.z};
        game::vec3 hi{e.pos.x, e.top, e.pos.z};
        game::vec3 edge{e.pos.x + b.right.x * e.hw, e.pos.y, e.pos.z + b.right.z * e.hw};
        bool vf = w2s(b, s.c.vfov, lo, w, h, foot, dz);
        bool vh = w2s(b, s.c.vfov, hi, w, h, head, dh);
        bool vs = w2s(b, s.c.vfov, edge, w, h, side, ds);
        (dz > 0 ? front : behind)++;
        if (!vf || !vh || !vs) continue;

        float top_y = std::min(foot.y, head.y);
        float bot_y = std::max(foot.y, head.y);
        float hh = bot_y - top_y;
        if (hh < 4.0f) continue;
        float cx = (foot.x + head.x) * 0.5f;
        float hw = std::max(std::fabs(side.x - cx), 3.0f);
        if (bot_y < 0 || top_y > h || cx + hw < 0 || cx - hw > w) continue;

        if (e.player && !cfg.player) continue;
        game::vec3 d{e.pos.x - b.pos.x, e.pos.y - b.pos.y, e.pos.z - b.pos.z};
        const float dist = std::sqrt(dot(d, d));
        if (dist > cfg.box_dist) continue;
        ++drawn;
        ImU32 col = e.player ? IM_COL32(90, 230, 120, 255) : IM_COL32(235, 60, 60, 255);
        if (cfg.boxes) {
            dl->AddRect(ImVec2(cx - hw, top_y), ImVec2(cx + hw, bot_y), IM_COL32(0, 0, 0, 170), 0.0f, 0, 3.5f);
            dl->AddRect(ImVec2(cx - hw, top_y), ImVec2(cx + hw, bot_y), col, 0.0f, 0, 1.5f);
        }

        if (cfg.skel && dist <= cfg.skel_dist) for (const auto& sg : e.bones) {
            pt p0, p1;
            float z0, z1;
            if (!w2s(b, s.c.vfov, sg.a, w, h, p0, z0) || !w2s(b, s.c.vfov, sg.b, w, h, p1, z1)) continue;
            dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), IM_COL32(0, 0, 0, 170), 3.0f);
            dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), IM_COL32(255, 255, 255, 230), 1.5f);
        }

        if (!cfg.labels || dist > cfg.label_dist) continue;
        char label[80];
        std::snprintf(label, sizeof label, "%s %.1fm", e.name, dist);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        ImVec2 tp(cx - ts.x * 0.5f, top_y - ts.y - 3.0f);
        dl->AddRectFilled(ImVec2(tp.x - 3.0f, tp.y - 1.0f), ImVec2(tp.x + ts.x + 3.0f, tp.y + ts.y + 1.0f),
                          IM_COL32(0, 0, 0, 150), 2.0f);
        dl->AddText(font, fs, tp, IM_COL32(255, 255, 255, 255), label);
    }

    const bool real_cam = b.pos.x != 0.0f || b.pos.y != 0.0f || b.pos.z != 0.0f;
    if (real_cam && front == 0 && behind > 0) {    // this shouldnt be needed
        if (++behind_streak > 90) {
            fwd_sign = -fwd_sign;
            behind_streak = 0;
            lg::line("everyone behind the camera for 90 frames, forward flipped to {}", fwd_sign);
        }
    } else {
        behind_streak = 0;
    }

    if (frames % 3600 == 1) {
        lg::line("cam pos {:.2f} {:.2f} {:.2f} fwd {:.2f} {:.2f} {:.2f} vfov {:.3f} ents {} seen {} locked {}",
                 b.pos.x, b.pos.y, b.pos.z, b.fwd.x, b.fwd.y, b.fwd.z, s.c.vfov, s.ents.size(), s.seen, s.locked);
    }
    return drawn;
}

}
