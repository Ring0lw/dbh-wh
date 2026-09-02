#include "game.hxx"

#include "cfg.hxx"
#include "lg.hxx"
#include "mem.hxx"
#include "offs.hxx"

#include <sched.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string_view>

namespace game {
namespace {

constexpr std::uint32_t lock_tok = 0x0dbee5;
constexpr std::uint32_t max_buckets = 1u << 16;
constexpr std::uint32_t max_chain = 4096;
constexpr std::uint32_t max_ents = 2048;
constexpr std::uint32_t max_bones = 512;
constexpr float min_lod = 20.0f;
constexpr float max_bone_len = 0.6f;

constexpr std::string_view skip[] = {"Gun", "Ghost", "Area", "TVSpeaker", "local ", "Camera"};    // parapluie stays

bool finite3(const vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool read_name(std::uintptr_t go, char* out, std::size_t cap) noexcept {
    out[0] = 0;
    auto sp = mem::rd<std::uintptr_t>(go + offs::go_name);
    if (!sp || !*sp) return false;
    auto cp = mem::rd<std::uintptr_t>(*sp);
    if (!cp || !*cp) return false;

    std::size_t n = cap - 1;
    while (n >= 8 && !mem::rd(*cp, out, n)) n /= 2;
    if (n < 8) return false;
    out[n] = 0;
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (c == 0) break;
        if (c < 0x20 || c > 0x7e) {
            out[i] = 0;
            break;
        }
    }
    return out[0] != 0;
}

std::uintptr_t deref(std::uintptr_t go) noexcept {
    if (!go) return 0;
    auto fl = mem::rd<std::uint32_t>(go + offs::go_flags);
    if (!fl || !(*fl & offs::go_loaded)) return 0;
    return mem::rd<std::uintptr_t>(go + offs::go_inst).value_or(0);
}

std::uintptr_t node_of(std::uintptr_t entity) noexcept {
    return deref(mem::rd<std::uintptr_t>(entity + offs::en_node).value_or(0));
}

bool near_root(const vec3& b, const vec3& root) noexcept {
    return finite3(b) && std::fabs(b.x - root.x) < 2.5f && std::fabs(b.z - root.z) < 2.5f &&
           b.y - root.y > -0.5f && b.y - root.y < 3.0f;
}

void skel(std::uintptr_t node, ent& e) noexcept {
    auto pose = mem::rd<std::uintptr_t>(node + offs::nd_pose);
    if (!pose || !*pose) return;
    auto nb = mem::rd<std::uint32_t>(*pose + offs::ps_nbones);
    auto bones = mem::rd<std::uintptr_t>(*pose + offs::ps_bones);
    if (!nb || !bones || !*bones || *nb == 0 || *nb > max_bones) return;
    const std::uint32_t n = *nb;

    static std::array<float, 16 * max_bones> buf;
    static std::array<std::int32_t, max_bones> par;
    static std::array<float, 4 * max_bones> desc;
    if (!mem::rd(*bones, buf.data(), n * 64)) return;

    bool found = false;
    float lo = e.pos.y, hi = e.pos.y, hw = 0.0f;
    for (std::uint32_t i = 0; i < n; ++i) {
        vec3 b{buf[16 * i + 12], buf[16 * i + 13], buf[16 * i + 14]};
        if (!near_root(b, e.pos)) continue;
        lo = std::min(lo, b.y);
        hi = std::max(hi, b.y);
        hw = std::max({hw, std::fabs(b.x - e.pos.x), std::fabs(b.z - e.pos.z)});
        found = true;
    }
    if (!found) return;
    float pad = std::clamp((hi - lo) * 0.08f, 0.02f, 0.12f);
    e.bot = lo - pad;
    e.top = hi + pad;
    e.hw = std::max(hw + pad, 0.05f);
    e.has_head = true;

    auto pp = mem::rd<std::uintptr_t>(*pose + offs::ps_parents);
    auto dp = mem::rd<std::uintptr_t>(*pose + offs::ps_desc);
    if (!pp || !*pp || !dp || !*dp) return;
    if (!mem::rd(*pp, par.data(), n * 4) || !mem::rd(*dp, desc.data(), n * 16)) return;

    const bool every = e.player && cfg::cur().player_all_bones;    // 495 on connor
    for (std::uint32_t i = 0; i < n; ++i) {
        if (!every && desc[4 * i + 1] < min_lod) continue;
        std::int32_t p = par[i];
        if (p < 0 || static_cast<std::uint32_t>(p) >= n) continue;
        vec3 a{buf[16 * i + 12], buf[16 * i + 13], buf[16 * i + 14]};
        vec3 b{buf[16 * p + 12], buf[16 * p + 13], buf[16 * p + 14]};
        if (!near_root(a, e.pos) || !near_root(b, e.pos)) continue;
        vec3 d{a.x - b.x, a.y - b.y, a.z - b.z};
        float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (len2 < (every ? 0.000001f : 0.0001f) || len2 > max_bone_len * max_bone_len) continue;
        e.bones.push_back({a, b});
    }
}

bool one(std::uintptr_t go, std::uintptr_t player_go, ent& e) noexcept {
    auto inst = mem::rd<std::uintptr_t>(go + offs::go_inst);
    if (!inst || !*inst) return false;

    std::uintptr_t body = deref(mem::rd<std::uintptr_t>(*inst + offs::ch_body).value_or(0));    // why is the node on the body and not the character
    if (!body) return false;
    std::uintptr_t node = node_of(body);
    if (!node) return false;

    auto pos = mem::rd<vec3>(node + offs::nd_pos);
    if (!pos || !finite3(*pos)) return false;
    if (pos->x == 0.0f && pos->y == 0.0f && pos->z == 0.0f) return false;

    if (!read_name(go, e.name, sizeof e.name)) std::strcpy(e.name, "?");
    for (std::string_view s : skip)
        if (std::string_view(e.name).find(s) != std::string_view::npos) return false;

    e.pos = *pos;
    e.bot = pos->y;
    e.top = pos->y + 1.75f;
    e.hw = 0.35f;
    e.cls = mem::rd<std::uint32_t>(go + offs::go_cls).value_or(0);
    e.player = go == player_go;
    e.has_head = false;
    e.bones.clear();
    skel(node, e);
    return true;
}

std::uintptr_t player_go() noexcept {
    auto svc = mem::rd<std::uintptr_t>(mem::base() + offs::gs_cfg);
    if (!svc || !*svc) return 0;
    auto tab = mem::rd<std::uintptr_t>(*svc + offs::gs_players);
    if (!tab || !*tab) return 0;
    const std::uintptr_t players = *tab + offs::pl_base;
    auto idx = mem::rd<std::uint32_t>(players + offs::pl_cur);
    if (!idx || *idx >= 4) return 0;
    return mem::rd<std::uintptr_t>(players + offs::pl_stride * *idx + offs::pl_go).value_or(0);
}

std::uintptr_t char_cls() noexcept {
    auto root = mem::rd<std::uintptr_t>(mem::base() + offs::cls_tab);
    if (!root || !*root) return 0;
    std::uintptr_t grp = *root + 16u * (offs::cls_char / 1000);
    auto n = mem::rd<std::uint32_t>(grp);
    auto arr = mem::rd<std::uintptr_t>(grp + 8);
    std::uint32_t idx = offs::cls_char % 1000;
    if (!n || !arr || !*arr || idx >= *n) return 0;
    return mem::rd<std::uintptr_t>(*arr + 8u * idx).value_or(0);
}

struct cls_guard {
    std::atomic_ref<std::uint32_t> owner;
    std::atomic_ref<std::uint32_t> depth;
    bool held = false;

    explicit cls_guard(std::uintptr_t cls) noexcept
        : owner(*reinterpret_cast<std::uint32_t*>(cls + offs::cls_lock)),
          depth(*reinterpret_cast<std::uint32_t*>(cls + offs::cls_depth)) {
        for (int i = 0; i < 20000; ++i) {
            std::uint32_t free = 0;
            if (owner.compare_exchange_strong(free, lock_tok, std::memory_order_acquire)) {
                held = true;
                break;
            }
            if (i > 200) sched_yield();
        }
        if (held) depth.fetch_add(1, std::memory_order_relaxed);
    }

    ~cls_guard() {
        if (!held) return;
        if (depth.fetch_sub(1, std::memory_order_relaxed) == 1) owner.store(0, std::memory_order_release);
    }
};

void read_cam(snap& s) noexcept {
    const std::uintptr_t base = mem::base();
    auto h = mem::rd<std::uintptr_t>(base + offs::cam_inst).value_or(0);
    std::uintptr_t node = mem::rd<std::uintptr_t>(base + offs::cam_node).value_or(0);
    if (!node && h) node = node_of(h);
    if (!node || !h) return;

    auto m = mem::rd<std::array<float, 16>>(node + offs::nd_world);
    auto fov = mem::rd<float>(h + offs::cm_vfov);
    auto asp = mem::rd<float>(h + offs::cm_aspect);
    if (!m || !fov || !(*fov > 0.05f && *fov < 3.1f)) return;
    for (float v : *m)
        if (!std::isfinite(v)) return;

    std::memcpy(s.c.m, m->data(), sizeof s.c.m);
    s.c.vfov = *fov;
    s.c.aspect = asp.value_or(0.0f);
    s.c.ok = true;
}

}

bool init() noexcept {
    const std::uintptr_t b = mem::base();
    if (!b) {
        lg::line("DetroitBecomeHuman.exe not mapped here, layer idle");
        return false;
    }
    auto root = mem::rd<std::uintptr_t>(b + offs::cls_tab);
    lg::line("exe base {:#x}, class table {:#x}, character class {:#x}", b, root.value_or(0), char_cls());
    return true;
}

void read(snap& s) noexcept {
    s.ents.clear();
    s.seen = 0;
    s.locked = false;
    s.c.ok = false;
    if (!mem::base()) return;

    read_cam(s);

    std::uintptr_t cls = char_cls();
    if (!cls) return;
    const std::uintptr_t pgo = player_go();

    cls_guard g(cls);
    if (!g.held) return;
    s.locked = true;

    auto buckets = mem::rd<std::uintptr_t>(cls + offs::cls_buckets);
    auto nb = mem::rd<std::uint32_t>(cls + offs::cls_nbuckets);
    if (!buckets || !*buckets || !nb || *nb > max_buckets) return;

    for (std::uint32_t i = 0; i < *nb && s.ents.size() < max_ents; ++i) {
        std::uintptr_t go = mem::rd<std::uintptr_t>(*buckets + 8u * i).value_or(0);
        for (std::uint32_t k = 0; go && k < max_chain; ++k) {
            ++s.seen;
            ent e;
            if (one(go, pgo, e)) s.ents.push_back(e);
            go = mem::rd<std::uintptr_t>(go + offs::go_next).value_or(0);
        }
    }
}

}
