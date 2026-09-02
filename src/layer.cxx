#include "draw.hxx"
#include "lg.hxx"

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <mutex>
#include <unordered_map>

#define dbh_export extern "C" __attribute__((visibility("default")))

namespace {

struct inst {
    PFN_vkGetInstanceProcAddr next = nullptr;
    PFN_vkDestroyInstance destroy = nullptr;
};

struct dev {
    PFN_vkGetDeviceProcAddr next = nullptr;
    PFN_vkDestroyDevice destroy = nullptr;
    PFN_vkQueuePresentKHR present = nullptr;
    PFN_vkCreateSwapchainKHR create_sc = nullptr;
    PFN_vkDestroySwapchainKHR destroy_sc = nullptr;
    PFN_vkGetDeviceQueue get_queue = nullptr;
    PFN_vkGetDeviceQueue2 get_queue2 = nullptr;
};

std::mutex lk;
std::unordered_map<void*, inst> insts;
std::unordered_map<void*, dev> devs;
VkInstance last_inst = VK_NULL_HANDLE;

void* key(const void* handle) noexcept { return *static_cast<void* const*>(handle); }

template <class T>
T* chain_info(const void* pnext, VkStructureType st, VkLayerFunction fn) noexcept {
    auto* p = static_cast<const VkBaseInStructure*>(pnext);
    while (p) {
        if (p->sType == st) {
            auto* c = reinterpret_cast<T*>(const_cast<VkBaseInStructure*>(p));
            if (c->function == fn) return c;
        }
        p = p->pNext;
    }
    return nullptr;
}

dev lookup(const void* handle) noexcept {
    std::lock_guard g(lk);
    auto it = devs.find(key(handle));
    return it == devs.end() ? dev{} : it->second;
}

VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* ac,
                                               VkInstance* out) noexcept {
    auto* link = chain_info<VkLayerInstanceCreateInfo>(ci->pNext, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,
                                                       VK_LAYER_LINK_INFO);
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;

    auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = create(ci, ac, out);
    if (r != VK_SUCCESS) return r;

    inst rec;
    rec.next = next;
    rec.destroy = reinterpret_cast<PFN_vkDestroyInstance>(next(*out, "vkDestroyInstance"));
    std::lock_guard g(lk);
    insts[key(*out)] = rec;
    last_inst = *out;
    lg::line("instance created");
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL destroy_instance(VkInstance instance, const VkAllocationCallbacks* ac) noexcept {
    PFN_vkDestroyInstance destroy = nullptr;
    {
        std::lock_guard g(lk);
        auto it = insts.find(key(instance));
        if (it == insts.end()) return;
        destroy = it->second.destroy;
        insts.erase(it);
        if (last_inst == instance) last_inst = VK_NULL_HANDLE;
    }
    if (destroy) destroy(instance, ac);
}

VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                             const VkAllocationCallbacks* ac, VkDevice* out) noexcept {
    auto* link = chain_info<VkLayerDeviceCreateInfo>(ci->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,
                                                     VK_LAYER_LINK_INFO);
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;

    auto create = reinterpret_cast<PFN_vkCreateDevice>(gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = create(phys, ci, ac, out);
    if (r != VK_SUCCESS) return r;

    dev rec;
    rec.next = gdpa;
    rec.destroy = reinterpret_cast<PFN_vkDestroyDevice>(gdpa(*out, "vkDestroyDevice"));
    rec.present = reinterpret_cast<PFN_vkQueuePresentKHR>(gdpa(*out, "vkQueuePresentKHR"));
    rec.create_sc = reinterpret_cast<PFN_vkCreateSwapchainKHR>(gdpa(*out, "vkCreateSwapchainKHR"));
    rec.destroy_sc = reinterpret_cast<PFN_vkDestroySwapchainKHR>(gdpa(*out, "vkDestroySwapchainKHR"));
    rec.get_queue = reinterpret_cast<PFN_vkGetDeviceQueue>(gdpa(*out, "vkGetDeviceQueue"));
    rec.get_queue2 = reinterpret_cast<PFN_vkGetDeviceQueue2>(gdpa(*out, "vkGetDeviceQueue2"));

    VkInstance instance = VK_NULL_HANDLE;
    {
        std::lock_guard g(lk);
        devs[key(*out)] = rec;
        instance = last_inst;
    }
    draw::add_dev(instance, phys, *out, gipa, gdpa, ci);
    lg::line("device created, present {}", rec.present ? "resolved" : "missing");
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice device, const VkAllocationCallbacks* ac) noexcept {
    draw::rm_dev(device);
    PFN_vkDestroyDevice destroy = nullptr;
    {
        std::lock_guard g(lk);
        auto it = devs.find(key(device));
        if (it == devs.end()) return;
        destroy = it->second.destroy;
        devs.erase(it);
    }
    if (destroy) destroy(device, ac);
}

VKAPI_ATTR VkResult VKAPI_CALL create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR* ci,
                                                const VkAllocationCallbacks* ac, VkSwapchainKHR* out) noexcept {
    dev d = lookup(device);
    if (!d.create_sc) return VK_ERROR_INITIALIZATION_FAILED;
    if (ci->oldSwapchain) draw::rm_chain(device, ci->oldSwapchain);
    VkResult r = d.create_sc(device, ci, ac, out);
    if (r != VK_SUCCESS) return r;
    draw::add_chain(device, *out, *ci);
    lg::line("swapchain {}x{} format {} images {}", ci->imageExtent.width, ci->imageExtent.height,
             static_cast<int>(ci->imageFormat), ci->minImageCount);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL destroy_swapchain(VkDevice device, VkSwapchainKHR sc,
                                             const VkAllocationCallbacks* ac) noexcept {
    draw::rm_chain(device, sc);
    dev d = lookup(device);
    if (d.destroy_sc) d.destroy_sc(device, sc, ac);
}

VKAPI_ATTR void VKAPI_CALL get_device_queue(VkDevice device, std::uint32_t family, std::uint32_t index,
                                            VkQueue* out) noexcept {
    dev d = lookup(device);
    if (!d.get_queue) return;
    d.get_queue(device, family, index, out);
    if (out && *out) draw::add_queue(device, family, *out);
}

VKAPI_ATTR void VKAPI_CALL get_device_queue2(VkDevice device, const VkDeviceQueueInfo2* qi, VkQueue* out) noexcept {
    dev d = lookup(device);
    if (!d.get_queue2) return;
    d.get_queue2(device, qi, out);
    if (out && *out) draw::add_queue(device, qi->queueFamilyIndex, *out);
}

VKAPI_ATTR VkResult VKAPI_CALL queue_present(VkQueue queue, const VkPresentInfoKHR* info) noexcept {
    dev d = lookup(queue);
    if (!d.present) return VK_ERROR_DEVICE_LOST;
    return draw::present(queue, info, d.present);
}

struct entry {
    const char* name;
    PFN_vkVoidFunction fn;
};

const entry inst_hooks[] = {
    {"vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(create_instance)},
    {"vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(destroy_instance)},
    {"vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(create_device)},
};

const entry dev_hooks[] = {
    {"vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(destroy_device)},
    {"vkCreateSwapchainKHR", reinterpret_cast<PFN_vkVoidFunction>(create_swapchain)},
    {"vkDestroySwapchainKHR", reinterpret_cast<PFN_vkVoidFunction>(destroy_swapchain)},
    {"vkGetDeviceQueue", reinterpret_cast<PFN_vkVoidFunction>(get_device_queue)},
    {"vkGetDeviceQueue2", reinterpret_cast<PFN_vkVoidFunction>(get_device_queue2)},
    {"vkQueuePresentKHR", reinterpret_cast<PFN_vkVoidFunction>(queue_present)},
};

PFN_vkVoidFunction hook(const entry* tab, std::size_t n, const char* name) noexcept {
    for (std::size_t i = 0; i < n; ++i)
        if (std::strcmp(tab[i].name, name) == 0) return tab[i].fn;
    return nullptr;
}

}

dbh_export VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL dbh_gdpa(VkDevice device, const char* name) noexcept {
    if (!device) return hook(dev_hooks, std::size(dev_hooks), name);
    PFN_vkGetDeviceProcAddr next = nullptr;
    {
        std::lock_guard g(lk);
        auto it = devs.find(key(device));
        if (it == devs.end()) return nullptr;
        next = it->second.next;
    }
    PFN_vkVoidFunction down = next ? next(device, name) : nullptr;
    if (PFN_vkVoidFunction mine = hook(dev_hooks, std::size(dev_hooks), name); mine && down) return mine;
    return down;
}

dbh_export VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL dbh_gipa(VkInstance instance, const char* name) noexcept {
    if (PFN_vkVoidFunction mine = hook(inst_hooks, std::size(inst_hooks), name)) return mine;
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(dbh_gipa);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(dbh_gdpa);
    if (!instance) return nullptr;
    PFN_vkGetInstanceProcAddr next = nullptr;
    {
        std::lock_guard g(lk);
        auto it = insts.find(key(instance));
        if (it == insts.end()) return nullptr;
        next = it->second.next;
    }
    if (!next) return nullptr;
    PFN_vkVoidFunction down = next(instance, name);
    if (PFN_vkVoidFunction mine = hook(dev_hooks, std::size(dev_hooks), name); mine && down) return mine;
    return down;
}

dbh_export VKAPI_ATTR VkResult VKAPI_CALL dbh_negotiate(VkNegotiateLayerInterface* v) noexcept {
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = dbh_gipa;
    v->pfnGetDeviceProcAddr = dbh_gdpa;
    v->pfnGetPhysicalDeviceProcAddr = nullptr;
    lg::line("layer negotiated");
    return VK_SUCCESS;
}
