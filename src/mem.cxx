#include "mem.hxx"

#include <sys/uio.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mem {

std::uintptr_t base() noexcept {
    static std::uintptr_t cached = 0;
    if (cached) return cached;

    FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return 0;

    const char tail[] = "DetroitBecomeHuman.exe";
    const std::size_t tl = sizeof tail - 1;
    std::uintptr_t lo = 0;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::size_t n = std::strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == ' ')) line[--n] = 0;
        if (n < tl || std::strcmp(line + n - tl, tail) != 0) continue;
        std::uintptr_t start = std::strtoull(line, nullptr, 16);
        if (!lo || start < lo) lo = start;
    }
    std::fclose(f);

    if (lo) {
        std::uint16_t mz = 0;
        if (rd(lo, &mz, sizeof mz) && mz == 0x5a4d) cached = lo;
    }
    return cached;
}

bool rd(std::uintptr_t addr, void* out, std::size_t n) noexcept {
    if (addr < 0x10000 || addr > 0x7fffffffffff) return false;
    iovec here{out, n};
    iovec there{reinterpret_cast<void*>(addr), n};
    return process_vm_readv(getpid(), &here, 1, &there, 1, 0) == static_cast<ssize_t>(n);
}

}
