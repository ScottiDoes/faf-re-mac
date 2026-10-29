// FAF addition, not in the shipped binary: see VulkanLoader.h.

#include "VulkanLoader.h"

#if defined(FAF_POSIX)
#include <dlfcn.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>

#include <climits>
#include <cstdint>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "gpg/core/utils/Logging.h"
#endif

namespace gpg::gal::vulkan
{
#if defined(FAF_POSIX)
  namespace
  {
    std::string sLoaderPath;
    std::string sDriverSelection;

    [[nodiscard]] bool IsSet(const char* const name) noexcept
    {
      const char* const value = std::getenv(name);
      return value != nullptr && value[0] != '\0';
    }

    [[nodiscard]] bool Exists(const std::string& path) noexcept
    {
      struct stat status{};
      return !path.empty() && ::stat(path.c_str(), &status) == 0;
    }

    /** The LunarG Vulkan SDK the build was configured with (a development fallback). */
    [[nodiscard]] std::string SdkDirectory()
    {
#if defined(FAF_VULKAN_SDK_DIR)
      return FAF_VULKAN_SDK_DIR;
#else
      return {};
#endif
    }

    /**
     * The Contents folder of the app bundle the game runs from, when that bundle carries
     * its own Vulkan loader (Contents/Frameworks/libvulkan.1.dylib, the release layout
     * macos/app/make_app.sh builds), else empty. The bundle's loader, MoltenVK and DXC win
     * over the SDK's.
     */
    [[nodiscard]] std::string BundleContents()
    {
#if defined(__APPLE__)
      char path[PATH_MAX]{};
      std::uint32_t size = sizeof(path);
      char resolved[PATH_MAX]{};
      if (::_NSGetExecutablePath(path, &size) != 0 || ::realpath(path, resolved) == nullptr) {
        return {};
      }
      std::string contents = resolved;
      for (int level = 0; level < 2; ++level) { // the executable, then its folder (Contents/MacOS)
        const std::size_t slash = contents.find_last_of('/');
        if (slash == std::string::npos || slash == 0U) {
          return {};
        }
        contents.resize(slash);
      }
      return Exists(contents + "/Frameworks/libvulkan.1.dylib") ? contents : std::string();
#else
      return {};
#endif
    }

    /**
     * Points the loader at a driver and at the validation layers before it is loaded
     * (it reads these variables when it scans for drivers and layers). Variables the
     * user set win. The driver is the app bundle's MoltenVK (else the SDK's), or the SDK's
     * KosmicKrisp with FAF_VULKAN_DRIVER=kosmickrisp (or any ICD manifest given by path).
     */
    void ConfigureLoaderEnvironment()
    {
      const std::string sdk = SdkDirectory();
      const std::string bundle = BundleContents();
      if (IsSet("VK_DRIVER_FILES") || IsSet("VK_ICD_FILENAMES") || IsSet("VK_ADD_DRIVER_FILES")) {
        const char* const name = IsSet("VK_DRIVER_FILES") ? "VK_DRIVER_FILES" : IsSet("VK_ICD_FILENAMES") ? "VK_ICD_FILENAMES" : "VK_ADD_DRIVER_FILES";
        sDriverSelection = std::string("as set by ") + name + "=" + std::getenv(name);
      } else {
        const char* const requested = std::getenv("FAF_VULKAN_DRIVER");
        std::string choice = (requested != nullptr) ? requested : "";
        std::string lower = choice;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string manifest;
        if (lower.size() > 5U && lower.compare(lower.size() - 5U, 5U, ".json") == 0) {
          manifest = choice;
        } else if (lower == "kosmickrisp" || lower == "kk") {
          lower = "kosmickrisp";
          manifest = sdk + "/share/vulkan/icd.d/libkosmickrisp_icd.json";
        } else {
          if (!lower.empty() && lower != "moltenvk" && lower != "mvk") {
            gpg::Warnf("Vulkan: FAF_VULKAN_DRIVER=%s is not moltenvk, kosmickrisp or a manifest path; using MoltenVK", choice.c_str());
          }
          lower = "moltenvk";
          manifest = bundle.empty() ? sdk + "/share/vulkan/icd.d/MoltenVK_icd.json"
                                    : bundle + "/Resources/vulkan/icd.d/MoltenVK_icd.json";
        }
        if (Exists(manifest) && (!sdk.empty() || !bundle.empty() || manifest == choice)) {
          ::setenv("VK_DRIVER_FILES", manifest.c_str(), 1);
          sDriverSelection = ((manifest == choice) ? std::string("manifest") : lower) + " (" + manifest + ")";
        } else {
          sDriverSelection = "the loader's own search (no " + lower + " manifest at " + manifest + ")";
        }
      }
      // The SDK's explicit layers (validation for /vulkandebug, the screenshot layer), for a
      // development build: an app bundle carries none and leaves the build machine's SDK alone.
      if (bundle.empty() && !IsSet("VK_LAYER_PATH") && !IsSet("VK_ADD_LAYER_PATH")) {
        const std::string layers = sdk + "/share/vulkan/explicit_layer.d";
        if (!sdk.empty() && Exists(layers)) {
          ::setenv("VK_ADD_LAYER_PATH", layers.c_str(), 1);
        }
      }
    }

    /**
     * The Vulkan loader of the POSIX build: FAF_VULKAN_LIBRARY when set, else the app
     * bundle's loader, else the configured Vulkan SDK's loader, else the Khronos loader by
     * name (Homebrew, DYLD_LIBRARY_PATH; it finds MoltenVK through its ICD manifest), else
     * MoltenVK itself, which exports vkGetInstanceProcAddr.
     */
    HMODULE OpenVulkanLibrary()
    {
      ConfigureLoaderEnvironment();
      if (const char* const configured = std::getenv("FAF_VULKAN_LIBRARY"); configured != nullptr && configured[0] != '\0') {
        void* const library = ::dlopen(configured, RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) {
          const char* const error = ::dlerror();
          gpg::Warnf("Vulkan: FAF_VULKAN_LIBRARY=%s does not load: %s", configured, (error != nullptr) ? error : "?");
        }
        return static_cast<HMODULE>(library);
      }
      const std::string bundle = BundleContents();
      std::string ownLoader;
      if (!bundle.empty()) {
        ownLoader = bundle + "/Frameworks/libvulkan.1.dylib";
      } else if (!SdkDirectory().empty()) {
        ownLoader = SdkDirectory() + "/lib/libvulkan.1.dylib";
      }
      if (Exists(ownLoader)) {
        if (void* const library = ::dlopen(ownLoader.c_str(), RTLD_NOW | RTLD_LOCAL); library != nullptr) {
          return static_cast<HMODULE>(library);
        }
      }
      constexpr const char* kCandidates[] = {
        "libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib", "libvulkan.so.1",
      };
      for (const char* const name : kCandidates) {
        if (void* const library = ::dlopen(name, RTLD_NOW | RTLD_LOCAL); library != nullptr) {
          return static_cast<HMODULE>(library);
        }
      }
      return nullptr;
    }
  } // namespace

  const char* LoaderPath() noexcept
  {
    return sLoaderPath.c_str();
  }

  const char* DriverSelection() noexcept
  {
    return sDriverSelection.c_str();
  }

  bool EnvironmentFlag(const char* const name) noexcept
  {
    const char* const value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
  }
#endif

  Functions& Vk()
  {
    static Functions functions;
    return functions;
  }

  bool Functions::LoadGlobal()
  {
    if (module != nullptr) {
      return true;
    }
#if defined(FAF_POSIX)
    module = OpenVulkanLibrary();
    if (module == nullptr) {
      gpg::Warnf("Vulkan: no Vulkan loader (FAF_VULKAN_LIBRARY, %s/lib/libvulkan.1.dylib, libvulkan.1.dylib, libMoltenVK.dylib)",
                 SdkDirectory().c_str());
      return false;
    }
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(::dlsym(module, "vkGetInstanceProcAddr"));
    if (Dl_info info{}; vkGetInstanceProcAddr != nullptr && ::dladdr(reinterpret_cast<const void*>(vkGetInstanceProcAddr), &info) != 0 &&
                        info.dli_fname != nullptr) {
      sLoaderPath = info.dli_fname;
    }
    gpg::Logf("Vulkan: loader %s, driver %s", sLoaderPath.empty() ? "?" : sLoaderPath.c_str(), sDriverSelection.c_str());
#else
    module = ::LoadLibraryA("vulkan-1.dll");
    if (module == nullptr) {
      return false;
    }
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(::GetProcAddress(module, "vkGetInstanceProcAddr"));
#endif
    if (vkGetInstanceProcAddr == nullptr) {
      Unload();
      return false;
    }
#define FAF_VK_LOAD_GLOBAL(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name));
    FAF_VK_GLOBAL_FUNCTIONS(FAF_VK_LOAD_GLOBAL)
#undef FAF_VK_LOAD_GLOBAL
    return vkCreateInstance != nullptr && vkEnumerateInstanceExtensionProperties != nullptr;
  }

  bool Functions::LoadInstance(const VkInstance instance)
  {
    bool complete = true;
#define FAF_VK_LOAD_INSTANCE(name)                                                     \
  name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));         \
  complete = complete && (name != nullptr);
    FAF_VK_INSTANCE_FUNCTIONS(FAF_VK_LOAD_INSTANCE)
#undef FAF_VK_LOAD_INSTANCE
#define FAF_VK_LOAD_OPTIONAL(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
    FAF_VK_DEBUG_FUNCTIONS(FAF_VK_LOAD_OPTIONAL)
#undef FAF_VK_LOAD_OPTIONAL
    return complete;
  }

  bool Functions::LoadDevice(const VkDevice device)
  {
    bool complete = true;
#define FAF_VK_LOAD_DEVICE(name)                                                       \
  name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));             \
  complete = complete && (name != nullptr);
    FAF_VK_DEVICE_FUNCTIONS(FAF_VK_LOAD_DEVICE)
#undef FAF_VK_LOAD_DEVICE
    return complete;
  }

  void Functions::Unload() noexcept
  {
    if (module != nullptr) {
#if defined(FAF_POSIX)
      ::dlclose(module);
#else
      ::FreeLibrary(module);
#endif
    }
    *this = Functions{};
  }
} // namespace gpg::gal::vulkan
