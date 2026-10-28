// POSIX replacement for gpg/gal/backends/vulkan/VulkanSurface.cpp (excluded in
// macos/exclusions.d/render.cmake): the platform half of the Vulkan backend.
//
// macOS presents through MoltenVK or KosmicKrisp with VK_EXT_metal_surface, whose
// create info takes a CAMetalLayer. The window system (platform/posix/shell, SDL3)
// owns every native window and gives each HWND's client area a CAMetalLayer sized in
// drawable pixels (shell::MetalLayer, Shell.h); the head's window is such an HWND.
// A window without a native window (the headless runner, or a shell that is not
// running) has no layer: the surface then fails as a bad window does on Windows, and
// DeviceVulkan::Setup throws its usual error.

#ifndef VK_USE_PLATFORM_METAL_EXT
#define VK_USE_PLATFORM_METAL_EXT
#endif
#include "gpg/gal/backends/vulkan/VulkanSurface.h"

#include "gpg/core/utils/Logging.h"
#include "platform/posix/shell/Shell.h"

namespace gpg::gal::vulkan
{
  const char* PlatformSurfaceExtension() noexcept
  {
    return VK_EXT_METAL_SURFACE_EXTENSION_NAME;
  }

  VkSurfaceKHR CreateWindowSurface(const VkInstance instance, void* const window, VkResult& result)
  {
    void* const layer = faf_posix::shell::MetalLayer(static_cast<HWND>(window));
    if (layer == nullptr) {
      gpg::Warnf(
        "Vulkan: window %p has no CAMetalLayer (%s)", window,
        faf_posix::shell::IsActive() ? "not a shell window" : "the window system is not running"
      );
      result = VK_ERROR_INITIALIZATION_FAILED;
      return VK_NULL_HANDLE;
    }
    const auto create =
      reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(Vk().vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (create == nullptr) {
      result = VK_ERROR_EXTENSION_NOT_PRESENT;
      return VK_NULL_HANDLE;
    }
    VkMetalSurfaceCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    info.pLayer = static_cast<const CAMetalLayer*>(layer);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    result = create(instance, &info, nullptr, &surface);
    return (result == VK_SUCCESS) ? surface : VK_NULL_HANDLE;
  }
} // namespace gpg::gal::vulkan
