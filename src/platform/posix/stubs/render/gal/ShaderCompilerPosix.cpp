// POSIX stand-in for gpg/gal/effects/ShaderCompiler.cpp (excluded in
// macos/exclusions.d/render.cmake), which loads dxcompiler.dll / dxil.dll and talks to
// them through WRL ComPtr and the Direct3D 12 reflection interfaces.
//
// Same front end on DXC's macOS build (libdxcompiler.dylib from the Vulkan SDK): the
// same argument lists, messages and per-thread compiler objects, through the plain-C++
// bridge in platform/posix/dxc (DXC's own COM headers cannot share a translation unit
// with the engine's Win32 stand-ins). Differences, all for the DXIL target, which only
// the Direct3D 12 backend uses and which does not exist on POSIX:
//   - there is no dxil.dll signer, so Compile/CompileRaw for DXIL fail with a message
//     (the SPIR-V target, the Vulkan backend's, is the whole of what runs here);
//   - ReflectInputs reads SPIR-V (effects/SpirvReflect) instead of DXIL through
//     ID3D12ShaderReflection, which only Windows has.
// Notes: macos/porting/shaders.md.

#include "gpg/gal/effects/ShaderCompiler.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/effects/SpirvReflect.h"
#include "platform/posix/dxc/DxcBridge.h"

namespace gpg::gal::effects
{
  // DXC itself is held by the bridge; a Library exists once it has loaded.
  struct ShaderCompiler::Library
  {
  };

  namespace
  {
    constexpr const char* kNoDxil = "DXIL is not available on this platform (no dxil.dll signer); only the SPIR-V target compiles";

    [[nodiscard]] bool TargetsSpirv(const std::vector<std::wstring>& arguments)
    {
      return std::find(arguments.begin(), arguments.end(), std::wstring(L"-spirv")) != arguments.end();
    }

    [[nodiscard]] bool IsSpirv(const std::vector<std::uint8_t>& code)
    {
      constexpr std::uint8_t kMagic[4] = {0x03U, 0x02U, 0x23U, 0x07U}; // 0x07230203, little endian
      return code.size() >= 20U && (code.size() % 4U) == 0U && std::equal(kMagic, kMagic + 4, code.begin());
    }
  } // namespace

  ShaderCompiler& ShaderCompiler::Instance()
  {
    static ShaderCompiler compiler;
    return compiler;
  }

  ShaderCompiler::ShaderCompiler()
  {
    const faf_posix::dxc::LoadStatus& status = faf_posix::dxc::Load();
    if (!status.loaded) {
      mLoadError = status.error;
      return;
    }
    mVersion = status.version;
    mLibrary = new Library();
    gpg::Logf("effects: %s loaded from %s", mVersion.c_str(), status.path.c_str());
  }

  ShaderCompiler::~ShaderCompiler()
  {
    // Process-lifetime singleton: the library stays loaded until exit.
    delete mLibrary;
    mLibrary = nullptr;
  }

  bool ShaderCompiler::Available() const noexcept
  {
    return mLibrary != nullptr;
  }

  const std::string& ShaderCompiler::LoadError() const noexcept
  {
    return mLoadError;
  }

  const std::string& ShaderCompiler::Version() const noexcept
  {
    return mVersion;
  }

  CompiledShader ShaderCompiler::Compile(
    const std::string_view hlsl, const ShaderEntry& entry, const ShaderTarget target, const std::vector<std::wstring>& extraArguments
  ) const
  {
    CompiledShader result;
    if (mLibrary == nullptr) {
      result.messages = mLoadError;
      return result;
    }
    if (target != ShaderTarget::Spirv) {
      result.messages = kNoDxil;
      return result;
    }

    const std::wstring entryName = faf_posix::dxc::Widen(entry.name);
    std::vector<std::wstring> arguments{
      L"-E", entryName,
      L"-T", entry.stage == ShaderStage::Vertex ? L"vs_6_0" : L"ps_6_0",
      // HLSL 2018: the effects predate 2021's stricter vector logic.
      L"-HV", L"2018",
      // Row-major matrices: the parameter store keeps the engine's layout.
      L"-Zpr",
      L"-O3",
      L"-D", entry.stage == ShaderStage::Vertex ? L"FAF_VERTEX_STAGE=1" : L"FAF_PIXEL_STAGE=1",
      // FXC accepted these silently.
      L"-Wno-conversion",
    };
    // The Vulkan backend's descriptor model: textures t<n> and samplers
    // s<n> in set 0 at bindings n and 64+n, $Globals alone in set 1.
    // D3D's clip-space y points the other way.
    arguments.insert(arguments.end(), {L"-spirv", L"-fvk-use-dx-layout", L"-fspv-reflect", L"-fspv-target-env=vulkan1.1",
                                       L"-fvk-t-shift", L"0", L"0", L"-fvk-s-shift", L"64", L"0", L"-fvk-b-shift", L"0", L"0",
                                       L"-fvk-bind-globals", L"0", L"1"});
    if (entry.stage == ShaderStage::Vertex) {
      arguments.push_back(L"-fvk-invert-y");
    }
    arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
    return Run(hlsl, arguments);
  }

  CompiledShader ShaderCompiler::CompileRaw(
    const std::string_view hlsl, const std::string_view entryName, const wchar_t* const profile, const std::vector<std::wstring>& extraArguments
  ) const
  {
    if (mLibrary == nullptr || !TargetsSpirv(extraArguments)) {
      CompiledShader result;
      result.messages = (mLibrary == nullptr) ? mLoadError : std::string(kNoDxil);
      return result;
    }
    std::vector<std::wstring> arguments{L"-E", faf_posix::dxc::Widen(entryName), L"-T", profile, L"-O3"};
    arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
    return Run(hlsl, arguments);
  }

  CompiledShader ShaderCompiler::Run(const std::string_view hlsl, const std::vector<std::wstring>& arguments) const
  {
    CompiledShader result;
    faf_posix::dxc::CompileOutput output = faf_posix::dxc::Compile(hlsl, arguments);
    result.messages = std::move(output.messages);
    if (!output.succeeded) {
      return result;
    }
    if (output.object.empty()) {
      result.messages += "\nno object produced";
      return result;
    }
    result.code = std::move(output.object);
    result.succeeded = true;
    return result;
  }

  std::vector<ShaderInput> ShaderCompiler::ReflectInputs(const std::vector<std::uint8_t>& code) const
  {
    // The Windows front end reads a DXIL input signature through ID3D12ShaderReflection;
    // there is no DXIL here. A SPIR-V shader's inputs come from its interface variables
    // instead, one entry per location as D3D lists one per register.
    std::vector<ShaderInput> inputs;
    if (mLibrary == nullptr || !IsSpirv(code)) {
      return inputs;
    }
    std::vector<SpirvInterfaceVariable> variables = ReflectSpirvInterface(code).inputs;
    std::stable_sort(variables.begin(), variables.end(), [](const SpirvInterfaceVariable& left, const SpirvInterfaceVariable& right) {
      return left.builtIn != right.builtIn ? !left.builtIn : left.location < right.location;
    });
    for (const SpirvInterfaceVariable& variable : variables) {
      for (std::uint32_t element = 0U; element < std::max(variable.locations, 1U); ++element) {
        ShaderInput input;
        input.semantic = variable.semantic;
        std::transform(input.semantic.begin(), input.semantic.end(), input.semantic.begin(), [](const unsigned char c) {
          return static_cast<char>(std::toupper(c));
        });
        input.index = variable.semanticIndex + element;
        input.component = (variable.component == SpirvInterfaceVariable::Component::Uint) ? ShaderInput::Component::Uint
          : (variable.component == SpirvInterfaceVariable::Component::Sint)                ? ShaderInput::Component::Sint
                                                                                            : ShaderInput::Component::Float;
        input.mask = (variable.components >= 4U) ? 0xFU : ((1U << variable.components) - 1U);
        input.systemValue = variable.builtIn;
        inputs.push_back(std::move(input));
      }
    }
    return inputs;
  }
} // namespace gpg::gal::effects
