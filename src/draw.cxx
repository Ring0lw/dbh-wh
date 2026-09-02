#include "draw.hxx"

#include "cfg.hxx"
#include "esp.hxx"
#include "game.hxx"
#include "input.hxx"
#include "lg.hxx"
#include "lua.hxx"
#include "menu.hxx"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace draw {
namespace {

#define dbh_fns(x)                                                                                    \
    x(vkGetDeviceQueue) x(vkGetSwapchainImagesKHR) x(vkCreateImageView) x(vkDestroyImageView)         \
    x(vkCreateRenderPass) x(vkDestroyRenderPass) x(vkCreateFramebuffer) x(vkDestroyFramebuffer)        \
    x(vkCreateCommandPool) x(vkDestroyCommandPool) x(vkAllocateCommandBuffers) x(vkFreeCommandBuffers) \
    x(vkCreateFence) x(vkDestroyFence) x(vkWaitForFences) x(vkResetFences) x(vkCreateSemaphore)        \
    x(vkDestroySemaphore) x(vkResetCommandBuffer) x(vkBeginCommandBuffer) x(vkEndCommandBuffer)        \
    x(vkCmdBeginRenderPass) x(vkCmdEndRenderPass) x(vkQueueSubmit) x(vkDeviceWaitIdle)

struct fns {
#define dbh_decl(n) PFN_##n n = nullptr;
    dbh_fns(dbh_decl)
#undef dbh_decl
};

struct frame {
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore done = VK_NULL_HANDLE;
};

struct chain {
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    VkExtent2D ext{};
    VkRenderPass pass = VK_NULL_HANDLE;
    std::vector<frame> frames;
    bool dead = false;
};

struct dev {
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice h = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    fns f;
    std::uint32_t family = ~0u;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::unordered_map<VkSwapchainKHR, chain> chains;
    std::unordered_map<VkQueue, std::uint32_t> qfam;
};

std::mutex lk;
std::unordered_map<VkDevice, dev> devs;
dev* ui_dev = nullptr;
VkRenderPass ui_pass = VK_NULL_HANDLE;
int game_state = 0;
game::snap snapshot;
std::chrono::steady_clock::time_point last;

template <class... A>
void once(bool& said, std::format_string<A...> f, A&&... a) noexcept {
    if (said) return;
    said = true;
    lg::line(f, std::forward<A>(a)...);
}

dev* find(VkDevice h) noexcept {
    auto it = devs.find(h);
    return it == devs.end() ? nullptr : &it->second;
}

PFN_vkVoidFunction load_fn(const char* name, void* user) noexcept {
    auto* d = static_cast<dev*>(user);
    if (PFN_vkVoidFunction p = d->gdpa(d->h, name)) return p;
    return d->gipa(d->inst, name);
}

bool make_pool(dev& d) noexcept {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = d.family;
    return d.f.vkCreateCommandPool(d.h, &ci, nullptr, &d.pool) == VK_SUCCESS;
}

VkRenderPass make_pass(dev& d, VkFormat fmt) noexcept {
    VkAttachmentDescription att{};
    att.format = fmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;

    VkRenderPass pass = VK_NULL_HANDLE;
    if (d.f.vkCreateRenderPass(d.h, &ci, nullptr, &pass) != VK_SUCCESS) return VK_NULL_HANDLE;
    return pass;
}

void teardown(dev& d, chain& c) noexcept {
    for (frame& f : c.frames) {
        if (f.done) d.f.vkDestroySemaphore(d.h, f.done, nullptr);
        if (f.fence) d.f.vkDestroyFence(d.h, f.fence, nullptr);
        if (f.fb) d.f.vkDestroyFramebuffer(d.h, f.fb, nullptr);
        if (f.view) d.f.vkDestroyImageView(d.h, f.view, nullptr);
        if (f.cmd) d.f.vkFreeCommandBuffers(d.h, d.pool, 1, &f.cmd);
    }
    c.frames.clear();
    if (c.pass) {
        if (ui_pass == c.pass) ui_pass = VK_NULL_HANDLE;
        d.f.vkDestroyRenderPass(d.h, c.pass, nullptr);
        c.pass = VK_NULL_HANDLE;
    }
}

bool build(dev& d, VkSwapchainKHR sc, chain& c) noexcept {
    std::uint32_t n = 0;
    if (d.f.vkGetSwapchainImagesKHR(d.h, sc, &n, nullptr) != VK_SUCCESS || n == 0) return false;
    std::vector<VkImage> images(n);
    if (d.f.vkGetSwapchainImagesKHR(d.h, sc, &n, images.data()) != VK_SUCCESS) return false;

    c.pass = make_pass(d, c.fmt);
    if (!c.pass) return false;

    c.frames.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        frame& f = c.frames[i];

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = c.fmt;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (d.f.vkCreateImageView(d.h, &vi, nullptr, &f.view) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = c.pass;
        fi.attachmentCount = 1;
        fi.pAttachments = &f.view;
        fi.width = c.ext.width;
        fi.height = c.ext.height;
        fi.layers = 1;
        if (d.f.vkCreateFramebuffer(d.h, &fi, nullptr, &f.fb) != VK_SUCCESS) return false;

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = d.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (d.f.vkAllocateCommandBuffers(d.h, &ai, &f.cmd) != VK_SUCCESS) return false;

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (d.f.vkCreateFence(d.h, &fci, nullptr, &f.fence) != VK_SUCCESS) return false;

        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (d.f.vkCreateSemaphore(d.h, &si, nullptr, &f.done) != VK_SUCCESS) return false;
    }
    return true;
}

bool start_ui(dev& d, chain& c) noexcept {
    const auto count = static_cast<std::uint32_t>(c.frames.size());
    if (ui_dev == &d) {
        if (ui_pass != c.pass) {
            d.f.vkDeviceWaitIdle(d.h);
            ImGui_ImplVulkan_SetMinImageCount(count < 2 ? 2 : count);
            ImGui_ImplVulkan_PipelineInfo pi{};
            pi.RenderPass = c.pass;
            pi.Subpass = 0;
            pi.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            ImGui_ImplVulkan_CreateMainPipeline(&pi);
            ui_pass = c.pass;
            lg::line("pipeline rebuilt for a new swapchain, {}x{}", c.ext.width, c.ext.height);    // four times on boot, why
        }
        return true;
    }
    if (ui_dev) return false;

    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        menu::style();
        input::hook();
    }
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1, load_fn, &d)) {
        lg::line("imgui could not load vulkan functions");
        return false;
    }

    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = d.inst;
    ii.PhysicalDevice = d.phys;
    ii.Device = d.h;
    ii.QueueFamily = d.family;
    ii.Queue = d.queue;
    ii.DescriptorPoolSize = 16;
    ii.MinImageCount = count < 2 ? 2 : count;
    ii.ImageCount = count < 2 ? 2 : count;
    ii.PipelineInfoMain.RenderPass = c.pass;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&ii)) {
        lg::line("ImGui_ImplVulkan_Init failed");
        return false;
    }
    ui_dev = &d;
    ui_pass = c.pass;
    lg::line("overlay up, {} images {}x{} format {}", count, c.ext.width, c.ext.height, static_cast<int>(c.fmt));
    return true;
}

}

void add_dev(VkInstance inst, VkPhysicalDevice phys, VkDevice h, PFN_vkGetInstanceProcAddr gipa,
             PFN_vkGetDeviceProcAddr gdpa, const VkDeviceCreateInfo* ci) noexcept {
    std::lock_guard g(lk);
    dev d;
    d.inst = inst;
    d.phys = phys;
    d.h = h;
    d.gipa = gipa;
    d.gdpa = gdpa;
#define dbh_load(n) d.f.n = reinterpret_cast<PFN_##n>(gdpa(h, #n));
    dbh_fns(dbh_load)
#undef dbh_load
    if (d.f.vkGetDeviceQueue && ci) {
        for (std::uint32_t i = 0; i < ci->queueCreateInfoCount; ++i) {
            const auto& q = ci->pQueueCreateInfos[i];
            for (std::uint32_t k = 0; k < q.queueCount; ++k) {
                VkQueue queue = VK_NULL_HANDLE;
                d.f.vkGetDeviceQueue(h, q.queueFamilyIndex, k, &queue);
                if (queue) d.qfam[queue] = q.queueFamilyIndex;
            }
        }
    }
    devs[h] = d;
}

void rm_dev(VkDevice h) noexcept {
    std::lock_guard g(lk);
    dev* d = find(h);
    if (!d) return;
    if (d->f.vkDeviceWaitIdle) d->f.vkDeviceWaitIdle(h);
    if (ui_dev == d) {
        ImGui_ImplVulkan_Shutdown();
        ui_dev = nullptr;
        ui_pass = VK_NULL_HANDLE;
    }
    for (auto& [sc, c] : d->chains) teardown(*d, c);
    if (d->pool) d->f.vkDestroyCommandPool(h, d->pool, nullptr);
    devs.erase(h);
}

void add_queue(VkDevice h, std::uint32_t family, VkQueue q) noexcept {
    std::lock_guard g(lk);
    if (dev* d = find(h); d && q) d->qfam[q] = family;
}

void add_chain(VkDevice h, VkSwapchainKHR sc, const VkSwapchainCreateInfoKHR& ci) noexcept {
    std::lock_guard g(lk);
    dev* d = find(h);
    if (!d) return;
    if (!(ci.imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        lg::line("swapchain without colour attachment usage, cannot draw into it");
        return;
    }
    chain c;
    c.fmt = ci.imageFormat;
    c.ext = ci.imageExtent;
    d->chains[sc] = c;
}

void rm_chain(VkDevice h, VkSwapchainKHR sc) noexcept {
    std::lock_guard g(lk);
    dev* d = find(h);
    if (!d) return;
    auto it = d->chains.find(sc);
    if (it == d->chains.end()) return;
    if (d->f.vkDeviceWaitIdle) d->f.vkDeviceWaitIdle(h);
    teardown(*d, it->second);
    d->chains.erase(it);
}

VkResult present(VkQueue q, const VkPresentInfoKHR* info, PFN_vkQueuePresentKHR next) noexcept {
    if (!info || info->swapchainCount == 0) return next(q, info);

    std::lock_guard g(lk);
    if (game_state == 0) game_state = game::init() ? 1 : -1;
    if (game_state < 0) return next(q, info);

    static bool said_chain = false, said_fam = false, said_pool = false, said_build = false, said_submit = false;

    dev* d = nullptr;
    chain* c = nullptr;
    for (auto& [h, rec] : devs) {
        auto it = rec.chains.find(info->pSwapchains[0]);
        if (it == rec.chains.end()) continue;
        d = &rec;
        c = &it->second;
        break;
    }
    if (!d || !c || c->dead) {
        if (!d) once(said_chain, "present on a swapchain never seen created");
        return next(q, info);
    }

    auto fam = d->qfam.find(q);
    if (fam == d->qfam.end()) {
        once(said_fam, "present queue was never handed out through vkGetDeviceQueue");
        return next(q, info);
    }
    if (d->pool && fam->second != d->family) {
        once(said_fam, "present on family {} but pool is family {}", fam->second, d->family);
        return next(q, info);
    }
    if (!d->pool) {
        d->family = fam->second;
        d->queue = q;
        if (!make_pool(*d)) {
            once(said_pool, "command pool failed");
            return next(q, info);
        }
    }
    if (c->frames.empty() && !build(*d, info->pSwapchains[0], *c)) {
        once(said_build, "swapchain resources failed, {}x{}", c->ext.width, c->ext.height);
        teardown(*d, *c);
        c->dead = true;
        return next(q, info);
    }
    if (!start_ui(*d, *c)) return next(q, info);

    const std::uint32_t idx = info->pImageIndices[0];
    if (idx >= c->frames.size()) return next(q, info);
    frame& f = c->frames[idx];

    const auto now = std::chrono::steady_clock::now();
    float dt = last.time_since_epoch().count() ? std::chrono::duration<float>(now - last).count() : 1.0f / 60.0f;
    last = now;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(c->ext.width), static_cast<float>(c->ext.height));
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
    input::drain(io);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    lua::tick();
    game::read(snapshot);
    const int drawn = esp::draw(ImGui::GetForegroundDrawList(), snapshot, io.DisplaySize.x, io.DisplaySize.y, cfg::cur());
    if (input::open()) menu::draw(drawn, static_cast<int>(snapshot.seen));
    ImGui::Render();

    d->f.vkWaitForFences(d->h, 1, &f.fence, VK_TRUE, UINT64_MAX);
    d->f.vkResetFences(d->h, 1, &f.fence);
    d->f.vkResetCommandBuffer(f.cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    d->f.vkBeginCommandBuffer(f.cmd, &bi);

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = c->pass;
    rp.framebuffer = f.fb;
    rp.renderArea = {{0, 0}, c->ext};
    d->f.vkCmdBeginRenderPass(f.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.cmd);
    d->f.vkCmdEndRenderPass(f.cmd);
    d->f.vkEndCommandBuffer(f.cmd);

    std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = info->waitSemaphoreCount;
    si.pWaitSemaphores = info->pWaitSemaphores;
    si.pWaitDstStageMask = stages.data();
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &f.done;
    if (d->f.vkQueueSubmit(q, 1, &si, f.fence) != VK_SUCCESS) {
        once(said_submit, "vkQueueSubmit failed, overlay skipped");
        return next(q, info);
    }

    VkPresentInfoKHR patched = *info;
    patched.waitSemaphoreCount = 1;
    patched.pWaitSemaphores = &f.done;
    return next(q, &patched);
}

}
