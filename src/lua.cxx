#include "lua.hxx"

#include "lg.hxx"
#include "mem.hxx"
#include "offs.hxx"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace lua {
namespace {

constexpr std::uint32_t t_bool = 1;
constexpr std::uint32_t t_num = 3;
constexpr std::uint32_t t_str = 4;
constexpr std::uint32_t t_tab = 5;

using run_buf_fn = __attribute__((ms_abi)) std::int64_t(void* ctx, const char* buf, int len, void* err);
using idle_fn = __attribute__((ms_abi)) int(void* self, int count);

struct ctx {
    std::uint64_t L = 0;
    std::uint64_t z0 = 0, z1 = 0, z2 = 0;
    std::int32_t a = -1;
    std::int32_t pad = 0;
    std::int32_t b = -1;
    std::int32_t c = 0;
};

set now;
int hooked = 0;
time_t seen = 0;
run_buf_fn* run_buf = nullptr;
idle_fn* orig_idle = nullptr;
std::atomic<int> pending{0};
std::atomic<std::int64_t> last_rc{-99};
std::atomic<unsigned> idles{0};
std::chrono::steady_clock::time_point last_stat;
std::chrono::steady_clock::time_point last_read;
std::string script_path;

std::string script() noexcept {
    if (!script_path.empty()) return script_path;
    const char* home = std::getenv("HOME");
    script_path = std::string(home ? home : "") +
                  "/.local/share/Steam/steamapps/common/Detroit Become Human/Immediate.lua";
    return script_path;
}

std::int64_t run_script(void* c) noexcept {
    static std::string buf;
    FILE* f = std::fopen(script().c_str(), "rb");
    if (!f) return -1;
    buf.clear();
    char tmp[4096];
    for (std::size_t n; (n = std::fread(tmp, 1, sizeof tmp, f)) > 0;) buf.append(tmp, n);
    std::fclose(f);
    if (buf.empty() || !run_buf) return -2;
    return run_buf(c, buf.data(), static_cast<int>(buf.size()), nullptr);
}

std::int64_t __attribute__((ms_abi)) run_file_hook(void* c, void*) noexcept {    // their fopen is the bigfile
    last_rc = run_script(c);
    return last_rc;
}

int __attribute__((ms_abi)) idle_hook(void* self, int count) noexcept {
    ++idles;
    if (pending.exchange(0)) {
        ctx c;
        const std::uintptr_t vm = mem::rd<std::uintptr_t>(mem::base() + offs::vm).value_or(0);
        std::uintptr_t cur = vm ? mem::rd<std::uintptr_t>(vm + offs::vm_ctx).value_or(0) : 0;
        if (cur) c.L = mem::rd<std::uint64_t>(cur + offs::ctx_state).value_or(0);
        if (!c.L && vm && mem::rd<std::int32_t>(vm + offs::vm_nstates).value_or(0) > 0)
            c.L = mem::rd<std::uint64_t>(vm + offs::vm_state).value_or(0);
        last_rc = c.L ? run_script(&c) : -3;
    }
    return orig_idle(self, count);
}

bool patch(std::uintptr_t addr, const void* bytes, std::size_t n, int prot) noexcept {
    const long page = ::sysconf(_SC_PAGESIZE);
    auto* aligned = reinterpret_cast<void*>(addr & ~static_cast<std::uintptr_t>(page - 1));
    if (::mprotect(aligned, static_cast<std::size_t>(page) * 2, prot) != 0) return false;
    std::memcpy(reinterpret_cast<void*>(addr), bytes, n);
    return true;
}

void install() noexcept {
    if (hooked) return;
    const std::uintptr_t base = mem::base();
    if (!base) return;
    hooked = -1;

    run_buf = reinterpret_cast<run_buf_fn*>(base + offs::run_buf);

    const std::uintptr_t dst = reinterpret_cast<std::uintptr_t>(&run_file_hook);
    unsigned char jmp[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};    // mov rax, imm64 ; jmp rax
    std::memcpy(jmp + 2, &dst, 8);
    if (!patch(base + offs::run_file, jmp, sizeof jmp, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        lg::line("run-file patch failed");
        return;
    }

    const std::uintptr_t slot = base + offs::idle_slot;    // mfc in a 2018 game
    auto cur = mem::rd<std::uintptr_t>(slot).value_or(0);
    if (cur != base + offs::idle_fn) {
        lg::line("idle vtable slot holds {:#x}, expected {:#x}, no lua", cur, base + offs::idle_fn);
        return;
    }
    orig_idle = reinterpret_cast<idle_fn*>(cur);
    const std::uintptr_t hook = reinterpret_cast<std::uintptr_t>(&idle_hook);
    if (!patch(slot, &hook, sizeof hook, PROT_READ | PROT_WRITE)) {
        lg::line("idle vtable patch failed");
        return;
    }
    hooked = 1;
    lg::line("lua hooks in, idle slot {:#x}, run-buffer {:#x}", slot, reinterpret_cast<std::uintptr_t>(run_buf));
}

std::uintptr_t state() noexcept {
    auto vm = mem::rd<std::uintptr_t>(mem::base() + offs::vm).value_or(0);
    if (!vm) return 0;
    auto n = mem::rd<std::int32_t>(vm + offs::vm_nstates).value_or(0);
    if (n <= 0) return 0;
    return mem::rd<std::uintptr_t>(vm + offs::vm_state).value_or(0);
}

std::string tstring(std::uintptr_t ts) noexcept {
    auto len = mem::rd<std::uint64_t>(ts + offs::ts_len).value_or(0);
    if (len == 0 || len > (1u << 16)) return {};
    std::string s(len, 0);
    if (!mem::rd(ts + offs::ts_chars, s.data(), len)) return {};
    return s;
}

struct val {
    std::uint64_t v = 0;
    std::uint32_t tt = 0;
};

val field(std::uintptr_t tab, std::string_view key) noexcept {
    if (!tab) return {};
    auto lsize = mem::rd<std::uint8_t>(tab + offs::tb_lsizenode).value_or(0);
    auto node = mem::rd<std::uintptr_t>(tab + offs::tb_node).value_or(0);
    if (!node || lsize > 16) return {};
    const std::uint32_t n = 1u << lsize;
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uintptr_t nd = node + 40u * i;
        auto ktt = mem::rd<std::uint32_t>(nd + 24);
        if (!ktt || *ktt != t_str) continue;
        auto ks = mem::rd<std::uintptr_t>(nd + 16).value_or(0);
        if (tstring(ks) != key) continue;
        val out;
        out.v = mem::rd<std::uint64_t>(nd).value_or(0);
        out.tt = mem::rd<std::uint32_t>(nd + 8).value_or(0);
        return out;
    }
    return {};
}

bool flag(std::uintptr_t tab, std::string_view key, bool dflt) noexcept {
    val f = field(tab, key);
    if (f.tt == t_bool) return (f.v & 0xff) != 0;
    if (f.tt == t_num) {
        double d;
        std::memcpy(&d, &f.v, sizeof d);
        return d != 0.0;
    }
    return dflt;
}

void read() noexcept {
    std::uintptr_t L = state();
    if (!L) return;
    auto gt = mem::rd<std::uintptr_t>(L + offs::ls_gt).value_or(0);
    if (!gt) return;

    val alive = field(gt, "dbh_alive");
    if (alive.tt == t_num) {
        double d;
        std::memcpy(&d, &alive.v, sizeof d);
        int a = static_cast<int>(d);
        if (a != now.alive) lg::line("lua alive {}", a);
        now.alive = a;
    }
    val note = field(gt, "dbh_note");
    if (note.tt == t_str) {
        std::string s = tstring(note.v);
        if (s != now.note) lg::line("lua note: {}", s);
        now.note = s;
    }
    val cfg = field(gt, "dbh");
    if (cfg.tt == t_tab) {
        now.boxes = flag(cfg.v, "boxes", true);
        now.skel = flag(cfg.v, "skel", true);
        now.labels = flag(cfg.v, "labels", true);
        now.player = flag(cfg.v, "player", true);
    }
}

}

void tick() noexcept {
    const auto t = std::chrono::steady_clock::now();
    install();
    if (hooked != 1) return;

    static std::int64_t said_rc = -99;
    static bool said_idle = false;
    const std::int64_t rc = last_rc.load();
    if (rc != said_rc) {
        said_rc = rc;
        lg::line("lua run returned {}", rc);
    }
    if (!said_idle && idles.load() > 0) {
        said_idle = true;
        lg::line("idle hook alive");
    }

    if (t - last_stat > std::chrono::milliseconds(500)) {
        last_stat = t;
        struct stat st{};
        if (::stat(script().c_str(), &st) == 0 && st.st_mtime != seen) {
            seen = st.st_mtime;
            pending = 1;
            lg::line("Immediate.lua changed, queued for the idle loop");
        }
    }

    if (t - last_read > std::chrono::milliseconds(250)) {
        last_read = t;
        read();
    }
}

const set& cur() noexcept { return now; }

}
