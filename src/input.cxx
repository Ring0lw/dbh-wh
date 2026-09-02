#include "input.hxx"

#include "lg.hxx"

#include "imgui.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>

namespace input {
namespace {

using check_fn = Bool (*)(Display*, XEvent*, Bool (*)(Display*, XEvent*, XPointer), XPointer);
using keysym_fn = KeySym (*)(XKeyEvent*, int);

struct ev {
    int type;
    float x, y;
    unsigned button;
    KeySym sym;
};

std::atomic<bool> hooked{false};
std::atomic<bool> menu{false};
check_fn orig = nullptr;
keysym_fn lookup = nullptr;
std::mutex lk;
std::deque<ev> queue;

bool is_input(int type) noexcept {
    return type == KeyPress || type == KeyRelease || type == ButtonPress || type == ButtonRelease ||
           type == MotionNotify;
}

void stash(const XEvent& e) noexcept {
    ev out{e.type, 0, 0, 0, 0};
    switch (e.type) {
    case MotionNotify:
        out.x = static_cast<float>(e.xmotion.x);
        out.y = static_cast<float>(e.xmotion.y);
        break;
    case ButtonPress:
    case ButtonRelease:
        out.x = static_cast<float>(e.xbutton.x);
        out.y = static_cast<float>(e.xbutton.y);
        out.button = e.xbutton.button;
        break;
    case KeyPress:
    case KeyRelease:
        out.sym = lookup ? lookup(const_cast<XKeyEvent*>(&e.xkey), 0) : 0;
        break;
    default:
        return;
    }
    std::lock_guard g(lk);
    if (queue.size() > 512) queue.pop_front();
    queue.push_back(out);
}

Bool hooked_check(Display* dpy, XEvent* e, Bool (*pred)(Display*, XEvent*, XPointer), XPointer arg) noexcept {
    for (;;) {
        if (!orig(dpy, e, pred, arg)) return False;
        if (!e) return True;
        if (e->type == KeyPress && lookup && lookup(&e->xkey, 0) == XK_Insert) {
            menu = !menu.load();
            lg::line("menu {}", menu.load() ? "open" : "closed");
            continue;
        }
        if (e->type == KeyRelease && lookup && lookup(&e->xkey, 0) == XK_Insert) continue;
        if (is_input(e->type)) {
            stash(*e);
            if (menu.load()) continue;
        } else if (e->type == GenericEvent && menu.load()) {
            continue;
        }
        return True;
    }
}

std::uintptr_t abs_addr(ElfW(Addr) bias, ElfW(Addr) v) noexcept { return v >= bias ? v : bias + v; }

bool patch_table(ElfW(Addr) bias, const ElfW(Rela)* tab, std::size_t bytes, const ElfW(Sym)* syms,
                 const char* strs) noexcept {
    for (std::size_t i = 0; i < bytes / sizeof(ElfW(Rela)); ++i) {
        const auto idx = ELF64_R_SYM(tab[i].r_info);
        if (!idx || std::strcmp(strs + syms[idx].st_name, "XCheckIfEvent") != 0) continue;
        auto** slot = reinterpret_cast<void**>(bias + tab[i].r_offset);
        const long page = ::sysconf(_SC_PAGESIZE);
        auto* al = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(slot) & ~static_cast<std::uintptr_t>(page - 1));
        if (::mprotect(al, static_cast<std::size_t>(page), PROT_READ | PROT_WRITE) != 0) return false;
        orig = reinterpret_cast<check_fn>(*slot);
        *slot = reinterpret_cast<void*>(&hooked_check);
        return true;
    }
    return false;
}

int visit(dl_phdr_info* info, std::size_t, void* out) noexcept {
    if (!info->dlpi_name || !std::strstr(info->dlpi_name, "winex11.so")) return 0;
    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            dyn = reinterpret_cast<const ElfW(Dyn)*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if (!dyn) return 0;

    const ElfW(Sym)* syms = nullptr;
    const char* strs = nullptr;
    const ElfW(Rela)* plt = nullptr;
    const ElfW(Rela)* rela = nullptr;
    std::size_t plt_sz = 0, rela_sz = 0;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
        case DT_SYMTAB: syms = reinterpret_cast<const ElfW(Sym)*>(abs_addr(info->dlpi_addr, dyn->d_un.d_ptr)); break;
        case DT_STRTAB: strs = reinterpret_cast<const char*>(abs_addr(info->dlpi_addr, dyn->d_un.d_ptr)); break;
        case DT_JMPREL: plt = reinterpret_cast<const ElfW(Rela)*>(abs_addr(info->dlpi_addr, dyn->d_un.d_ptr)); break;
        case DT_PLTRELSZ: plt_sz = dyn->d_un.d_val; break;
        case DT_RELA: rela = reinterpret_cast<const ElfW(Rela)*>(abs_addr(info->dlpi_addr, dyn->d_un.d_ptr)); break;
        case DT_RELASZ: rela_sz = dyn->d_un.d_val; break;
        default: break;
        }
    }
    if (!syms || !strs) return 0;
    bool* done = static_cast<bool*>(out);
    if ((plt && patch_table(info->dlpi_addr, plt, plt_sz, syms, strs)) ||
        (rela && patch_table(info->dlpi_addr, rela, rela_sz, syms, strs))) {
        *done = true;
        lg::line("XCheckIfEvent patched in {}", info->dlpi_name);
    }
    return 1;
}

ImGuiKey key_of(KeySym s) noexcept {
    switch (s) {
    case XK_Escape: return ImGuiKey_Escape;
    case XK_Return: return ImGuiKey_Enter;
    case XK_Left: return ImGuiKey_LeftArrow;
    case XK_Right: return ImGuiKey_RightArrow;
    case XK_Up: return ImGuiKey_UpArrow;
    case XK_Down: return ImGuiKey_DownArrow;
    case XK_Tab: return ImGuiKey_Tab;
    case XK_space: return ImGuiKey_Space;
    default: return ImGuiKey_None;
    }
}

}

bool hook() noexcept {
    if (hooked.load()) return true;
    void* x11 = ::dlopen("libX11.so.6", RTLD_NOLOAD | RTLD_LAZY);
    if (!x11) {
        lg::line("libX11 not loaded in this process, no menu input");
        return false;
    }
    lookup = reinterpret_cast<keysym_fn>(::dlsym(x11, "XLookupKeysym"));
    bool done = false;
    ::dl_iterate_phdr(visit, &done);
    if (!done) {
        lg::line("winex11.so not found or XCheckIfEvent not in its relocations, no menu input");
        return false;
    }
    hooked = true;
    return true;
}

void drain(ImGuiIO& io) noexcept {
    std::deque<ev> batch;
    {
        std::lock_guard g(lk);
        batch.swap(queue);
    }
    io.MouseDrawCursor = menu.load();
    for (const ev& e : batch) {
        switch (e.type) {
        case MotionNotify:
            io.AddMousePosEvent(e.x, e.y);
            break;
        case ButtonPress:
        case ButtonRelease: {
            const bool down = e.type == ButtonPress;
            io.AddMousePosEvent(e.x, e.y);
            if (e.button == Button1) io.AddMouseButtonEvent(0, down);
            else if (e.button == Button3) io.AddMouseButtonEvent(1, down);
            else if (e.button == Button2) io.AddMouseButtonEvent(2, down);
            else if (e.button == Button4 && down) io.AddMouseWheelEvent(0.0f, 1.0f);
            else if (e.button == Button5 && down) io.AddMouseWheelEvent(0.0f, -1.0f);
            break;
        }
        case KeyPress:
        case KeyRelease: {
            ImGuiKey k = key_of(e.sym);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, e.type == KeyPress);
            break;
        }
        default:
            break;
        }
    }
}

bool open() noexcept { return menu.load(); }

}
