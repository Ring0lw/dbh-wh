#include "lg.hxx"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace lg {
namespace {

std::mutex lk;
FILE* file = nullptr;

}

void put(std::string_view s) noexcept {
    std::lock_guard g(lk);
    if (!file) {
        const char* home = std::getenv("HOME");
        std::string path = home ? std::string(home) + "/.dbh_esp.log" : std::string("/tmp/dbh_esp.log");
        file = std::fopen(path.c_str(), "a");
        if (!file) return;
        std::setvbuf(file, nullptr, _IOLBF, 0);
    }
    std::fprintf(file, "[%d] %.*s\n", static_cast<int>(getpid()), static_cast<int>(s.size()), s.data());
}

}
