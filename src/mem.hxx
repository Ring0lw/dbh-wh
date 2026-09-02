#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace mem {

std::uintptr_t base() noexcept;
bool rd(std::uintptr_t addr, void* out, std::size_t n) noexcept;

template <class T>
std::optional<T> rd(std::uintptr_t addr) noexcept {
    T v;
    if (!rd(addr, &v, sizeof v)) return std::nullopt;
    return v;
}

}
