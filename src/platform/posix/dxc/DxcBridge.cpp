// DXC bridge: see DxcBridge.h. Built without the engine's Win32 stand-ins
// (macos/dxc.cmake): the COM types here are DXC's own (WinAdapter.h).

#include "DxcBridge.h"

#include <dlfcn.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "dxcapi.h" // includes WinAdapter.h off Windows

namespace faf_posix::dxc
{
  namespace
  {
    constexpr const char* kLibraryName = "libdxcompiler.dylib";

    DxcCreateInstanceProc gCreate = nullptr; // set once by Load(), before any Compile

    /**
     * A compiler and utils object per thread, as the Windows front end keeps them: one
     * object must not be used by two threads at once, but separate objects compile in
     * parallel (the background shader workers and the render thread).
     */
    struct ThreadDxc
    {
      CComPtr<IDxcUtils> utils;
      CComPtr<IDxcCompiler3> compiler;
    };

    [[nodiscard]] ThreadDxc* ThisThreadDxc()
    {
      thread_local ThreadDxc instance;
      thread_local bool tried = false;
      if (!tried) {
        tried = true;
        if (gCreate == nullptr || FAILED(gCreate(CLSID_DxcUtils, IID_PPV_ARGS(&instance.utils))) ||
            FAILED(gCreate(CLSID_DxcCompiler, IID_PPV_ARGS(&instance.compiler)))) {
          instance.utils.Release();
          instance.compiler.Release();
        }
      }
      return (instance.compiler != nullptr) ? &instance : nullptr;
    }

    [[nodiscard]] std::string WithSlash(std::string directory)
    {
      if (!directory.empty() && directory.back() != '/') {
        directory += '/';
      }
      return directory;
    }

    [[nodiscard]] std::string ExecutableDirectory()
    {
      char path[PATH_MAX]{};
      std::uint32_t size = sizeof(path);
      if (::_NSGetExecutablePath(path, &size) != 0) {
        return {};
      }
      char resolved[PATH_MAX]{};
      const std::string executable = (::realpath(path, resolved) != nullptr) ? resolved : path;
      const std::size_t slash = executable.find_last_of('/');
      return (slash == std::string::npos) ? std::string() : executable.substr(0U, slash + 1U);
    }

    [[nodiscard]] bool FileExists(const std::string& path)
    {
      struct stat info{};
      return ::stat(path.c_str(), &info) == 0;
    }

    void LoadOnce(LoadStatus& status)
    {
      std::vector<std::string> candidates;
      if (const char* const configured = std::getenv("FAF_DXC_DIR"); configured != nullptr && configured[0] != '\0') {
        candidates.push_back(WithSlash(configured) + kLibraryName);
      }
      const std::string directory = ExecutableDirectory();
      if (!directory.empty()) {
        // The app bundle's own copy (Contents/Frameworks, macos/app/make_app.sh) wins over the SDK's.
        candidates.push_back(directory + "../Frameworks/" + kLibraryName);
      }
#if defined(FAF_VULKAN_SDK_DIR)
      candidates.push_back(std::string(FAF_VULKAN_SDK_DIR "/lib/") + kLibraryName);
#endif
      if (!directory.empty()) {
        candidates.push_back(directory + kLibraryName);
      }
      candidates.push_back(kLibraryName); // DYLD_LIBRARY_PATH and the default search path

      void* module = nullptr;
      std::string loadFailure; // a library that exists but would not load (architecture, signing)
      for (const std::string& candidate : candidates) {
        const bool bare = candidate.find('/') == std::string::npos;
        if (!bare && !FileExists(candidate)) {
          continue;
        }
        module = ::dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (module != nullptr) {
          status.path = candidate;
          break;
        }
        if (!bare && loadFailure.empty()) {
          const char* const reason = ::dlerror();
          loadFailure = std::string(": ") + candidate + ": " + ((reason != nullptr) ? reason : "dlopen failed");
        }
      }
      if (module == nullptr) {
        status.error = std::string(kLibraryName) + " not found in FAF_DXC_DIR, the app's Frameworks, the Vulkan SDK"
#if defined(FAF_VULKAN_SDK_DIR)
          " (" FAF_VULKAN_SDK_DIR "/lib)"
#endif
          " or next to the executable" +
          loadFailure;
        return;
      }

      const auto create = reinterpret_cast<DxcCreateInstanceProc>(::dlsym(module, "DxcCreateInstance"));
      CComPtr<IDxcUtils> utils;
      CComPtr<IDxcCompiler3> compiler;
      if (create == nullptr || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
          FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        status.error = std::string(kLibraryName) + " does not provide IDxcCompiler3";
        status.path.clear();
        utils.Release();
        compiler.Release();
        ::dlclose(module);
        return;
      }

      // A digest of the loaded compiler for shader cache keys. The Windows front end
      // records major.minor and whether dxil.dll signs; here the commit as well, so a
      // different DXC build never reuses another's SPIR-V.
      char version[128]{};
      UINT32 major = 0U;
      UINT32 minor = 0U;
      CComPtr<IDxcVersionInfo2> versionInfo;
      if (SUCCEEDED(compiler.QueryInterface(&versionInfo)) && SUCCEEDED(versionInfo->GetVersion(&major, &minor))) {
        UINT32 commits = 0U;
        char* hash = nullptr;
        if (SUCCEEDED(versionInfo->GetCommitInfo(&commits, &hash)) && hash != nullptr) {
          std::snprintf(version, sizeof(version), "dxc %u.%u (%u %s)", major, minor, commits, hash);
          CoTaskMemFree(hash); // WinAdapter.h: free
        } else {
          std::snprintf(version, sizeof(version), "dxc %u.%u", major, minor);
        }
        status.version = version;
      } else if (CComPtr<IDxcVersionInfo> plain; SUCCEEDED(compiler.QueryInterface(&plain)) && SUCCEEDED(plain->GetVersion(&major, &minor))) {
        std::snprintf(version, sizeof(version), "dxc %u.%u", major, minor);
        status.version = version;
      } else {
        status.version = "dxc";
      }

      gCreate = create; // the module stays loaded for the life of the process
      status.loaded = true;
    }
  } // namespace

  const LoadStatus& Load()
  {
    static LoadStatus status;
    static std::once_flag once;
    std::call_once(once, [] { LoadOnce(status); });
    return status;
  }

  CompileOutput Compile(const std::string_view source, const std::vector<std::wstring>& arguments)
  {
    CompileOutput output;
    const LoadStatus& status = Load();
    if (!status.loaded) {
      output.messages = status.error;
      return output;
    }

    std::vector<LPCWSTR> argumentPointers;
    argumentPointers.reserve(arguments.size());
    for (const std::wstring& argument : arguments) {
      argumentPointers.push_back(argument.c_str());
    }

    DxcBuffer buffer{};
    buffer.Ptr = source.data();
    buffer.Size = source.size();
    buffer.Encoding = DXC_CP_UTF8;

    ThreadDxc* const dxc = ThisThreadDxc();
    if (dxc == nullptr) {
      output.messages = "DxcCreateInstance failed on this thread";
      return output;
    }
    CComPtr<IDxcResult> result;
    const HRESULT hr = dxc->compiler->Compile(
      &buffer, argumentPointers.data(), static_cast<UINT32>(argumentPointers.size()), nullptr, IID_PPV_ARGS(&result)
    );
    if (FAILED(hr) || result == nullptr) {
      char message[64]{};
      std::snprintf(message, sizeof(message), "IDxcCompiler3::Compile failed (0x%08X)", static_cast<unsigned>(hr));
      output.messages = message;
      return output;
    }

    CComPtr<IDxcBlobUtf8> errors;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors != nullptr &&
        errors->GetStringLength() != 0U) {
      output.messages.assign(errors->GetStringPointer(), errors->GetStringLength());
    }
    HRESULT compileStatus = E_FAIL;
    result->GetStatus(&compileStatus);
    if (FAILED(compileStatus)) {
      return output;
    }
    if (result->HasOutput(DXC_OUT_HLSL)) {
      CComPtr<IDxcBlobUtf8> text;
      if (SUCCEEDED(result->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&text), nullptr)) && text != nullptr) {
        output.text.assign(text->GetStringPointer(), text->GetStringLength());
      }
    }
    if (result->HasOutput(DXC_OUT_OBJECT)) {
      CComPtr<IDxcBlob> object;
      if (SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) && object != nullptr) {
        const auto* const bytes = static_cast<const std::uint8_t*>(object->GetBufferPointer());
        output.object.assign(bytes, bytes + object->GetBufferSize());
      }
    }
    output.succeeded = true;
    return output;
  }

  std::wstring Widen(const std::string_view utf8)
  {
    static_assert(sizeof(wchar_t) == 4, "DXC takes UTF-32 wchar_t text off Windows");
    std::wstring wide;
    wide.reserve(utf8.size());
    std::size_t index = 0U;
    while (index < utf8.size()) {
      const auto lead = static_cast<unsigned char>(utf8[index]);
      std::size_t length = 0U;
      char32_t codePoint = 0U;
      if (lead < 0x80U) {
        length = 1U;
        codePoint = lead;
      } else if ((lead & 0xE0U) == 0xC0U) {
        length = 2U;
        codePoint = lead & 0x1FU;
      } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3U;
        codePoint = lead & 0x0FU;
      } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4U;
        codePoint = lead & 0x07U;
      }
      bool valid = length != 0U && index + length <= utf8.size();
      for (std::size_t next = 1U; valid && next < length; ++next) {
        const auto continuation = static_cast<unsigned char>(utf8[index + next]);
        valid = (continuation & 0xC0U) == 0x80U;
        codePoint = (codePoint << 6U) | (continuation & 0x3FU);
      }
      constexpr char32_t kMinimum[5] = {0U, 0U, 0x80U, 0x800U, 0x10000U};
      valid = valid && codePoint >= kMinimum[length] && codePoint <= 0x10FFFFU && (codePoint < 0xD800U || codePoint > 0xDFFFU);
      if (!valid) {
        wide.push_back(static_cast<wchar_t>(0xFFFD));
        ++index;
        continue;
      }
      wide.push_back(static_cast<wchar_t>(codePoint));
      index += length;
    }
    return wide;
  }
} // namespace faf_posix::dxc
