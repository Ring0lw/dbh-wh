#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace draw {

void add_dev(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, PFN_vkGetInstanceProcAddr gipa,
             PFN_vkGetDeviceProcAddr gdpa, const VkDeviceCreateInfo* ci) noexcept;
void rm_dev(VkDevice dev) noexcept;
void add_queue(VkDevice dev, std::uint32_t family, VkQueue q) noexcept;
void add_chain(VkDevice dev, VkSwapchainKHR sc, const VkSwapchainCreateInfoKHR& ci) noexcept;
void rm_chain(VkDevice dev, VkSwapchainKHR sc) noexcept;
VkResult present(VkQueue q, const VkPresentInfoKHR* info, PFN_vkQueuePresentKHR next) noexcept;

}
