#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace lg {

void put(std::string_view s) noexcept;

template <class... A>
void line(std::format_string<A...> f, A&&... a) noexcept {
    put(std::format(f, std::forward<A>(a)...));
}

}
