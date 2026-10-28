// Standalone probe (no engine): how the Apple Vulkan drivers (MoltenVK, KosmicKrisp)
// report surface extents and acquire/present results for an SDL3 window's CAMetalLayer
// across a resize, a minimize and a restore. Informs DeviceVulkan's swap-chain handling.
//
//   probe_swapchain [moltenvk|kosmickrisp] [frames]
//
// The window is small, unfocusable and lives for a few seconds (GUI etiquette).

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_METAL_EXT
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef FAF_VULKAN_SDK_DIR
#error "FAF_VULKAN_SDK_DIR"
#endif

namespace
{
  PFN_vkGetInstanceProcAddr gpa = nullptr;
#define G(name) auto name = reinterpret_cast<PFN_##name>(gpa(instance, #name))

  const char* R(VkResult r)
  {
    switch (r) {
    case VK_SUCCESS: return "SUCCESS";
    case VK_SUBOPTIMAL_KHR: return "SUBOPTIMAL";
    case VK_ERROR_OUT_OF_DATE_KHR: return "OUT_OF_DATE";
    case VK_TIMEOUT: return "TIMEOUT";
    case VK_NOT_READY: return "NOT_READY";
    case VK_ERROR_SURFACE_LOST_KHR: return "SURFACE_LOST";
    default: return "other";
    }
  }
} // namespace

int main(int argc, char** argv)
{
  const std::string driver = (argc > 1) ? argv[1] : "moltenvk";
  const int frames = (argc > 2) ? std::atoi(argv[2]) : 150;
  const std::string sdk = FAF_VULKAN_SDK_DIR;
  const std::string icd = sdk + ((driver == "kosmickrisp") ? "/share/vulkan/icd.d/libkosmickrisp_icd.json" : "/share/vulkan/icd.d/MoltenVK_icd.json");
  setenv("VK_DRIVER_FILES", icd.c_str(), 1);
  void* lib = dlopen((sdk + "/lib/libvulkan.1.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
  if (lib == nullptr) {
    std::printf("no loader\n");
    return 1;
  }
  gpa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::printf("SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow(
    "faf vulkan probe", 320, 200, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_METAL
  );
  SDL_MetalView view = SDL_Metal_CreateView(window);
  void* layer = SDL_Metal_GetLayer(view);
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(window, &pw, &ph);
  std::printf("window 320x200 pt, %dx%d px, scale %.2f\n", pw, ph, SDL_GetWindowDisplayScale(window));

  VkInstance instance = VK_NULL_HANDLE;
  G(vkCreateInstance);
  const char* exts[] = {"VK_KHR_surface", "VK_EXT_metal_surface", "VK_KHR_portability_enumeration"};
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = 3;
  ici.ppEnabledExtensionNames = exts;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
    std::printf("vkCreateInstance failed\n");
    return 1;
  }
  G(vkEnumeratePhysicalDevices);
  G(vkCreateMetalSurfaceEXT);
  G(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
  G(vkEnumerateDeviceExtensionProperties);
  G(vkCreateDevice);
  G(vkGetDeviceProcAddr);
  G(vkGetPhysicalDeviceProperties2);
  uint32_t n = 1;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  vkEnumeratePhysicalDevices(instance, &n, &phys);
  VkPhysicalDeviceDriverProperties drv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &drv};
  vkGetPhysicalDeviceProperties2(phys, &p2);
  std::printf("driver %s %s\n", drv.driverName, drv.driverInfo);

  VkMetalSurfaceCreateInfoEXT sci{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
  sci.pLayer = static_cast<const CAMetalLayer*>(layer);
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  std::printf("create surface: %s\n", R(vkCreateMetalSurfaceEXT(instance, &sci, nullptr, &surface)));

  uint32_t extCount = 0;
  vkEnumerateDeviceExtensionProperties(phys, nullptr, &extCount, nullptr);
  std::vector<VkExtensionProperties> available(extCount);
  vkEnumerateDeviceExtensionProperties(phys, nullptr, &extCount, available.data());
  std::vector<const char*> dexts{"VK_KHR_swapchain"};
  for (auto& e : available) {
    if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) {
      dexts.push_back("VK_KHR_portability_subset");
    }
  }
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<uint32_t>(dexts.size());
  dci.ppEnabledExtensionNames = dexts.data();
  VkDevice device = VK_NULL_HANDLE;
  vkCreateDevice(phys, &dci, nullptr, &device);
#define D(name) auto name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name))
  D(vkCreateSwapchainKHR);
  D(vkDestroySwapchainKHR);
  D(vkGetSwapchainImagesKHR);
  D(vkAcquireNextImageKHR);
  D(vkQueuePresentKHR);
  D(vkGetDeviceQueue);
  D(vkCreateCommandPool);
  D(vkAllocateCommandBuffers);
  D(vkBeginCommandBuffer);
  D(vkEndCommandBuffer);
  D(vkCmdPipelineBarrier);
  D(vkCmdClearColorImage);
  D(vkQueueSubmit);
  D(vkCreateSemaphore);
  D(vkCreateFence);
  D(vkWaitForFences);
  D(vkResetFences);
  D(vkResetCommandPool);
  D(vkDeviceWaitIdle);
  VkQueue queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, 0, 0, &queue);

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{};
  std::vector<VkImage> images;
  auto caps = [&](const char* what) {
    VkSurfaceCapabilitiesKHR c{};
    const VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &c);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    std::printf("  [%s] caps %s current %ux%u min %ux%u max %ux%u images %u..%u; window px %dx%d flags%s%s\n", what, R(r),
                c.currentExtent.width, c.currentExtent.height, c.minImageExtent.width, c.minImageExtent.height, c.maxImageExtent.width,
                c.maxImageExtent.height, c.minImageCount, c.maxImageCount, w, h,
                (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) ? " MINIMIZED" : "",
                (SDL_GetWindowFlags(window) & SDL_WINDOW_OCCLUDED) ? " OCCLUDED" : "");
    return c;
  };
  auto create = [&](bool useCurrent, VkExtent2D requested) {
    const VkSurfaceCapabilitiesKHR c = caps("create");
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = 3;
    info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    extent = useCurrent ? c.currentExtent : requested;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    const VkResult r = vkCreateSwapchainKHR(device, &info, nullptr, &created);
    std::printf("  create swapchain %ux%u: %s\n", extent.width, extent.height, R(r));
    if (swapchain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapchain = created;
    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    images.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
  };
  create(true, {});

  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  VkCommandPool pool = VK_NULL_HANDLE;
  vkCreateCommandPool(device, &pci, nullptr, &pool);
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cai.commandPool = pool;
  cai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &cai, &cmd);
  VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore acquired = VK_NULL_HANDLE;
  VkSemaphore done = VK_NULL_HANDLE;
  vkCreateSemaphore(device, &semi, nullptr, &acquired);
  vkCreateSemaphore(device, &semi, nullptr, &done);
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(device, &fci, nullptr, &fence);

  std::string lastAcquire, lastPresent;
  const Uint64 started = SDL_GetTicks();
  for (int frame = 0; frame < frames; ++frame) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
        std::printf("  f%d event 0x%x (%d,%d)\n", frame, event.type, event.window.data1, event.window.data2);
      }
    }
    if (frame == 30) {
      std::printf("f%d: resize to 400x260 pt\n", frame);
      SDL_SetWindowSize(window, 400, 260);
      SDL_SyncWindow(window);
      caps("after resize");
    }
    if (frame == 70) {
      std::printf("f%d: minimize\n", frame);
      SDL_MinimizeWindow(window);
      SDL_SyncWindow(window);
      caps("after minimize");
    }
    if (frame == 100) {
      std::printf("f%d: restore\n", frame);
      SDL_RestoreWindow(window);
      SDL_SyncWindow(window);
      caps("after restore");
    }
    uint32_t index = 0;
    const Uint64 t0 = SDL_GetTicksNS();
    VkResult ar = vkAcquireNextImageKHR(device, swapchain, 2000000000ULL, acquired, VK_NULL_HANDLE, &index);
    const Uint64 t1 = SDL_GetTicksNS();
    if (lastAcquire != R(ar) || (t1 - t0) > 100000000ULL) {
      std::printf("f%d acquire %s (%.1f ms)\n", frame, R(ar), (t1 - t0) / 1e6);
      lastAcquire = R(ar);
    }
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
      vkDeviceWaitIdle(device);
      create(true, {});
      continue;
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
      continue;
    }
    vkResetCommandPool(device, pool, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = images[index];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkClearColorValue color{};
    color.float32[0] = (frame % 60) / 60.0f;
    color.float32[1] = 0.3f;
    color.float32[2] = 0.6f;
    color.float32[3] = 1.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquired;
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &done;
    vkQueueSubmit(queue, 1, &si, fence);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &index;
    const VkResult pr = vkQueuePresentKHR(queue, &pi);
    if (lastPresent != R(pr)) {
      std::printf("f%d present %s\n", frame, R(pr));
      lastPresent = R(pr);
    }
    vkWaitForFences(device, 1, &fence, VK_TRUE, ~0ULL);
    vkResetFences(device, 1, &fence);
    if (frame == 50 && pr == VK_SUBOPTIMAL_KHR) {
      std::printf("f%d: recreate at the engine's old size %ux%u\n", frame, extent.width, extent.height);
      vkDeviceWaitIdle(device);
      create(false, extent);
    }
    if (frame == 55) {
      std::printf("f%d: recreate at current extent\n", frame);
      vkDeviceWaitIdle(device);
      create(true, {});
    }
  }
  std::printf("%d frames in %.2f s\n", frames, (SDL_GetTicks() - started) / 1000.0);
  vkDeviceWaitIdle(device);
  SDL_Metal_DestroyView(view);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
