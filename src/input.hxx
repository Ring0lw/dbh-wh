#pragma once

struct ImGuiIO;

namespace input {

bool hook() noexcept;
void drain(ImGuiIO& io) noexcept;
bool open() noexcept;

}
