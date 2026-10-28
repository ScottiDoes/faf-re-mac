// The game's effects through the macOS shader path, as DeviceVulkan::CreateEffect runs it:
//   d3d10states.compat + <effect>.fx (CD3DEffect::InitEffectFromFile's merged buffer)
//   -> faf_posix::gal::PreprocessEffect with DeviceVulkan's injected macros
//   -> d3d11::TranslateEffectSource(text, true, {})
//   -> effects::EffectProgram (SPIR-V) -> CompileAll (every entry point of every pass)
// then writes each module as the device would hand it to Vulkan (StripSpirvReflection;
// pixel shaders also linked to each pass's vertex shader as DeviceVulkan::ModuleFor does)
// for spirv-val / spirv-cross (run_effects.sh). A second EffectProgram over the same text
// must come entirely from the shader cache with identical code. Also compiles the
// GPU-driven programs (moho/render/*Shader.h) as GpuDrivenVulkan does.
//
// Usage: effects_test <compat> <out dir> <label> <effect.fx>...
// Env: FAF_SHADER_CACHE_DIR (the script points it at a fresh directory),
//      FAF_TEST_PARALLEL=1 (prepare every effect on the background shader workers
//      while the main thread compiles too: per-thread DXC objects under contention).

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/backends/d3d11/EffectSourceD3D11.h"
#include "gpg/gal/effects/EffectRuntime.h"
#include "gpg/gal/effects/ShaderCompiler.h"
#include "gpg/gal/effects/SpirvReflect.h"
#include "moho/render/ProjectileIconShader.h"
#include "moho/render/SelectionBracketShader.h"
#include "moho/render/StrategicIconClassifyShader.h"
#include "moho/render/StrategicIconShader.h"
#include "moho/render/VisionShader.h"
#include "moho/render/WorldShapeShader.h"
#include "platform/posix/gal/HlslPreprocess.h"

namespace
{
  using namespace gpg::gal;
  using Clock = std::chrono::steady_clock;

  // DeviceVulkan.cpp kInjectedMacros, same order.
  const std::vector<std::pair<std::string, std::string>> kInjectedMacros = {
    {"technique", "technique10"}, {"VERSION", "DIRECT3D10"}, {"vs_1_1", "vs_4_0"},   {"vs_1_3", "vs_4_0"},
    {"vs_1_4", "vs_4_0"},         {"vs_2_0", "vs_4_0"},      {"vs_3_0", "vs_4_0"},   {"ps_1_1", "ps_4_0"},
    {"ps_1_3", "ps_4_0"},         {"ps_1_4", "ps_4_0"},      {"ps_2_0", "ps_4_0"},   {"ps_2_a", "ps_4_0"},
    {"ps_2_b", "ps_4_0"},         {"ps_3_0", "ps_4_0"},      {"MipFilter", "Filter"}, {"MinFilter", "Filter"},
    {"MagFilter", "Filter"},      {"NONE", "MIN_MAG_MIP_POINT"}, {"LINEAR", "MIN_MAG_MIP_LINEAR"},
    {"POINT", "MIN_MAG_MIP_POINT"}, {"VULKAN", "1"},
  };

  class StderrLog final : public gpg::LogTarget
  {
  public:
    StderrLog() : LogTarget(true) {}
    void OnMessage(gpg::LogSeverity level, const msvc8::string& message, const msvc8::vector<msvc8::string>&, int) override
    {
      std::fprintf(stderr, "[log %d] %s\n", static_cast<int>(level), message.c_str());
    }
  };

  /**
   * As the headless runner leaves (HeadlessApp.cpp): std::_Exit, without the engine's
   * static destructors and atexit handlers (CDiskWatch's DISK_ResetWatch locks a mutex
   * that is already destroyed by then and aborts the process).
   */
  [[noreturn]] void QuickExit(const int code)
  {
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
  }

  double Ms(const Clock::time_point from, const Clock::time_point to)
  {
    return std::chrono::duration<double, std::milli>(to - from).count();
  }

  std::string ReadFile(const std::string& path)
  {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
  }

  bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
  }

  void WriteText(const std::string& path, const std::string& text)
  {
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  std::string Base(const std::string& path)
  {
    const std::size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1U);
  }

  std::string Safe(std::string name)
  {
    for (char& c : name) {
      if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) {
        c = '_';
      }
    }
    return name;
  }

  int CountFiles(const std::string& directory)
  {
    int count = 0;
    if (DIR* const dir = ::opendir(directory.c_str())) {
      while (const dirent* const entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() > 4U && (name.ends_with(".spv"))) {
          ++count;
        }
      }
      ::closedir(dir);
    }
    return count;
  }

  /** DeviceVulkan::ModuleFor's stage link: pixel inputs moved onto the vertex outputs, by semantic. */
  std::vector<std::uint8_t> LinkPixel(const effects::CompiledEntry& vertex, const effects::CompiledEntry& pixel, int& unwritten)
  {
    const effects::SpirvInterface vertexInterface = effects::ReflectSpirvInterface(vertex.code);
    const effects::SpirvInterface pixelInterface = effects::ReflectSpirvInterface(pixel.code);
    std::map<std::uint32_t, std::uint32_t> locations;
    std::uint32_t spare = 31U;
    for (const effects::SpirvInterfaceVariable& input : pixelInterface.inputs) {
      if (input.builtIn) {
        continue;
      }
      std::uint32_t location = 0xFFFFFFFFU;
      for (const effects::SpirvInterfaceVariable& output : vertexInterface.outputs) {
        if (!output.builtIn && output.semantic == input.semantic && input.semanticIndex >= output.semanticIndex &&
            input.semanticIndex < output.semanticIndex + output.locations) {
          location = output.location + (input.semanticIndex - output.semanticIndex);
        }
      }
      if (location == 0xFFFFFFFFU) {
        location = spare--;
        ++unwritten;
      }
      locations[input.id] = location;
    }
    return effects::StripSpirvReflection(effects::PatchSpirvInputLocations(pixel.code, locations));
  }

  struct Totals
  {
    int effects = 0;
    int effectFailures = 0;
    int techniques = 0;
    int passes = 0;
    int entries = 0;
    int compiled = 0;
    int failed = 0;
    int warnings = 0;
    int linkedModules = 0;
    int unwrittenInputs = 0;
    int cacheMismatches = 0;
    int reflectMismatches = 0;
    double preprocessMs = 0.0;
    double compileMs = 0.0;
    double cachedMs = 0.0;
  };

  void RunEffect(const std::string& compat, const std::string& fxPath, const std::string& outRoot, Totals& totals,
                 std::shared_ptr<effects::EffectProgram>* backgroundProgram)
  {
    const std::string base = Base(fxPath);
    const std::string vfsName = "/effects/" + base;
    const std::string merged = compat + ReadFile(fxPath);
    const std::string outDir = outRoot + "/" + Safe(base);
    ::mkdir(outDir.c_str(), 0755);
    ++totals.effects;

    const Clock::time_point t0 = Clock::now();
    const faf_posix::gal::PreprocessedEffect preprocessed = faf_posix::gal::PreprocessEffect(merged, vfsName.c_str(), kInjectedMacros);
    const Clock::time_point t1 = Clock::now();
    if (!preprocessed.succeeded) {
      std::printf("FAIL %s preprocess:\n%s\n", vfsName.c_str(), preprocessed.errors.c_str());
      ++totals.effectFailures;
      return;
    }
    if (!preprocessed.errors.empty()) {
      std::printf("WARN %s preprocess warnings:\n%s\n", vfsName.c_str(), preprocessed.errors.c_str());
    }
    WriteText(outDir + "/preprocessed.fx", preprocessed.text);

    d3d11::TranslatedEffectSource translated = d3d11::TranslateEffectSource(preprocessed.text, true, std::string_view{});
    const Clock::time_point t2 = Clock::now();
    WriteText(outDir + "/translated.fx", translated.source);
    for (const std::string& note : translated.notes) {
      std::printf("NOTE %s translator: %s\n", vfsName.c_str(), note.c_str());
    }

    std::shared_ptr<effects::EffectProgram> program;
    try {
      program = std::make_shared<effects::EffectProgram>(vfsName, translated.source, effects::ShaderTarget::Spirv, merged);
    } catch (const std::exception& error) {
      std::printf("FAIL %s EffectProgram: %s\n", vfsName.c_str(), error.what());
      ++totals.effectFailures;
      return;
    }
    const Clock::time_point t3 = Clock::now();
    WriteText(outDir + "/generated.hlsl", program->Modern().hlsl);
    if (backgroundProgram != nullptr) {
      effects::EffectProgram::PrepareInBackground(program);
      *backgroundProgram = program;
    }

    const effects::EffectDescription& description = program->Description();
    int passes = 0;
    int missingShaders = 0;
    for (std::uint32_t technique = 0U; technique < description.techniques.size(); ++technique) {
      for (std::uint32_t pass = 0U; pass < description.techniques[technique].passes.size(); ++pass) {
        ++passes;
        const effects::PassEntries& entries = program->Entries(technique, pass);
        const effects::Pass& passDescription = description.techniques[technique].passes[pass];
        if ((entries.vertex < 0 && !passDescription.vertexShader.entry.empty()) ||
            (entries.pixel < 0 && !passDescription.pixelShader.entry.empty())) {
          ++missingShaders;
        }
      }
    }

    program->CompileAll();
    const Clock::time_point t4 = Clock::now();

    int compiled = 0;
    int failed = 0;
    const std::size_t entryCount = program->Modern().entries.size();
    for (std::uint32_t entry = 0U; entry < entryCount; ++entry) {
      const effects::CompiledEntry& result = program->Shader(entry);
      const effects::ShaderEntry& shaderEntry = program->Modern().entries[entry];
      if (!result.succeeded) {
        ++failed;
        std::printf("FAIL %s entry %s = %s(%s):\n%s\n", vfsName.c_str(), shaderEntry.name.c_str(), shaderEntry.function.c_str(),
                    shaderEntry.arguments.c_str(), result.messages.substr(0U, 3000U).c_str());
        continue;
      }
      ++compiled;
      if (!result.messages.empty()) {
        ++totals.warnings;
        std::printf("WARN %s entry %s compiled with messages:\n%s\n", vfsName.c_str(), shaderEntry.name.c_str(),
                    result.messages.substr(0U, 600U).c_str());
      }
      const std::string stem = outDir + "/" + Safe(shaderEntry.name);
      WriteFile(stem + ".raw.spv", result.code);
      WriteFile(stem + ".spv", effects::StripSpirvReflection(result.code));
      if (shaderEntry.stage == effects::ShaderStage::Vertex) {
        // ShaderCompiler::ReflectInputs on SPIR-V must agree with the interface reflection.
        const std::vector<effects::ShaderInput> inputs = effects::ShaderCompiler::Instance().ReflectInputs(result.code);
        std::size_t expected = 0U;
        for (const effects::SpirvInterfaceVariable& variable : effects::ReflectSpirvInterface(result.code).inputs) {
          expected += std::max(variable.locations, 1U);
        }
        if (inputs.size() != expected || (expected != 0U && inputs.front().semantic.empty())) {
          ++totals.reflectMismatches;
          std::printf("REFLECT %s %s: %zu inputs, interface says %zu\n", vfsName.c_str(), shaderEntry.name.c_str(), inputs.size(), expected);
        }
      }
    }

    // The pass pairings the device links.
    std::set<std::pair<std::int32_t, std::int32_t>> pairs;
    for (std::uint32_t technique = 0U; technique < description.techniques.size(); ++technique) {
      for (std::uint32_t pass = 0U; pass < description.techniques[technique].passes.size(); ++pass) {
        const effects::PassEntries& entries = program->Entries(technique, pass);
        if (entries.vertex >= 0 && entries.pixel >= 0) {
          pairs.emplace(entries.vertex, entries.pixel);
        }
      }
    }
    int unwritten = 0;
    for (const auto& [vertex, pixel] : pairs) {
      const effects::CompiledEntry& vertexEntry = program->Shader(static_cast<std::uint32_t>(vertex));
      const effects::CompiledEntry& pixelEntry = program->Shader(static_cast<std::uint32_t>(pixel));
      if (!vertexEntry.succeeded || !pixelEntry.succeeded) {
        continue;
      }
      WriteFile(outDir + "/" + Safe(pixelEntry.entryName) + ".linked-" + Safe(vertexEntry.entryName) + ".spv",
                LinkPixel(vertexEntry, pixelEntry, unwritten));
      ++totals.linkedModules;
    }

    // Again, from the cache: same code, no compiles.
    const Clock::time_point t5 = Clock::now();
    effects::EffectProgram again(vfsName, translated.source, effects::ShaderTarget::Spirv, merged);
    again.CompileAll();
    const Clock::time_point t6 = Clock::now();
    int mismatches = 0;
    for (std::uint32_t entry = 0U; entry < entryCount; ++entry) {
      const effects::CompiledEntry& first = program->Shader(entry);
      const effects::CompiledEntry& second = again.Shader(entry);
      if (first.succeeded != second.succeeded || first.code != second.code || first.key != second.key) {
        ++mismatches;
      }
    }

    std::printf(
      "EFFECT %-34s techniques=%3zu passes=%3d entries=%3zu compiled=%3d failed=%d missing=%d linked=%zu unwritten-inputs=%d "
      "preprocess=%.1fms translate=%.1fms program=%.1fms compile=%.0fms cached=%.1fms cache-mismatch=%d text=%zuB notes=%zu\n",
      vfsName.c_str(), description.techniques.size(), passes, entryCount, compiled, failed, missingShaders, pairs.size(), unwritten,
      Ms(t0, t1), Ms(t1, t2), Ms(t2, t3), Ms(t3, t4), Ms(t5, t6), mismatches, preprocessed.text.size(),
      translated.notes.size() + description.notes.size() + program->Modern().notes.size()
    );
    totals.techniques += static_cast<int>(description.techniques.size());
    totals.passes += passes;
    totals.entries += static_cast<int>(entryCount);
    totals.compiled += compiled;
    totals.failed += failed + missingShaders;
    totals.unwrittenInputs += unwritten;
    totals.cacheMismatches += mismatches;
    totals.preprocessMs += Ms(t0, t1);
    totals.compileMs += Ms(t3, t4);
    totals.cachedMs += Ms(t5, t6);
  }

  struct GpuProgram
  {
    const char* name;
    const char* source;
    std::size_t bytes;
    const char* vertex;
    const char* pixel;
    const char* compute;
  };

  /** GpuDrivenVulkan.cpp CompileModule: the same arguments. */
  effects::CompiledShader CompileGpuModule(const GpuProgram& program, const char* const entry, const wchar_t* const profile, const bool vertex)
  {
    std::vector<std::wstring> arguments{
      L"-HV", L"2018", L"-spirv", L"-fvk-use-dx-layout", L"-fspv-target-env=vulkan1.1",
      L"-fvk-b-shift", L"0", L"0", L"-fvk-t-shift", L"1", L"0", L"-fvk-u-shift", L"7", L"0", L"-fvk-s-shift", L"9", L"0",
    };
    if (vertex) {
      arguments.push_back(L"-fvk-invert-y");
    }
    return effects::ShaderCompiler::Instance().CompileRaw(std::string_view(program.source, program.bytes), entry, profile, arguments);
  }

  int RunGpuPrograms(const std::string& outRoot)
  {
    const GpuProgram programs[] = {
      {"CastVision", moho::kVisionShaderSource, sizeof(moho::kVisionShaderSource) - 1U, "CastVisionVS", "CastVisionPS", nullptr},
      {"ProjectileIcon", moho::kProjectileIconShaderSource, sizeof(moho::kProjectileIconShaderSource) - 1U, "ProjectileIconVS", "ProjectileIconPS", nullptr},
      {"SelectionBracket", moho::kSelectionBracketShaderSource, sizeof(moho::kSelectionBracketShaderSource) - 1U, "SelectionBracketVS", "SelectionBracketPS", nullptr},
      {"StrategicIcon", moho::kStrategicIconShaderSource, sizeof(moho::kStrategicIconShaderSource) - 1U, "StrategicIconVS", "StrategicIconPS", nullptr},
      {"ClassifyIcons", moho::kStrategicIconClassifyShaderSource, sizeof(moho::kStrategicIconClassifyShaderSource) - 1U, nullptr, nullptr, "ClassifyIconsCS"},
      {"CompactIcons", moho::kStrategicIconClassifyShaderSource, sizeof(moho::kStrategicIconClassifyShaderSource) - 1U, nullptr, nullptr, "CompactIconsCS"},
      {"WorldShape", moho::kWorldShapeShaderSource, sizeof(moho::kWorldShapeShaderSource) - 1U, "WorldShapeVS", "WorldShapePS", nullptr},
    };
    const std::string outDir = outRoot + "/gpu-driven";
    ::mkdir(outDir.c_str(), 0755);
    int failures = 0;
    int modules = 0;
    for (const GpuProgram& program : programs) {
      const struct
      {
        const char* entry;
        const wchar_t* profile;
        bool vertex;
      } stages[] = {{program.vertex, L"vs_6_0", true}, {program.pixel, L"ps_6_0", false}, {program.compute, L"cs_6_0", false}};
      for (const auto& stage : stages) {
        if (stage.entry == nullptr) {
          continue;
        }
        const Clock::time_point t0 = Clock::now();
        const effects::CompiledShader compiled = CompileGpuModule(program, stage.entry, stage.profile, stage.vertex);
        const double ms = Ms(t0, Clock::now());
        if (!compiled.succeeded) {
          ++failures;
          std::printf("FAIL gpu-driven %s %s:\n%s\n", program.name, stage.entry, compiled.messages.c_str());
          continue;
        }
        ++modules;
        WriteFile(outDir + "/" + program.name + "." + stage.entry + ".spv", compiled.code);
        std::printf("GPU %s %s %zu bytes %.0fms%s%s\n", program.name, stage.entry, compiled.code.size(), ms,
                    compiled.messages.empty() ? "" : " warnings: ", compiled.messages.substr(0U, 300U).c_str());
      }
    }
    std::printf("GPU-DRIVEN modules=%d failures=%d\n", modules, failures);
    return failures;
  }

  int RunFrontEndChecks()
  {
    int failures = 0;
    effects::ShaderCompiler& compiler = effects::ShaderCompiler::Instance();
    // DXIL is not offered on POSIX.
    effects::ShaderEntry entry;
    entry.name = "VS";
    entry.function = "VS";
    const effects::CompiledShader dxil = compiler.Compile("float4 VS() : SV_Position { return 0; }", entry, effects::ShaderTarget::Dxil);
    std::printf("CHECK dxil target: succeeded=%d message=%s\n", dxil.succeeded, dxil.messages.c_str());
    failures += dxil.succeeded ? 1 : 0;
    const effects::CompiledShader raw = compiler.CompileRaw("float4 VS() : SV_Position { return 0; }", "VS", L"vs_6_0", {L"-HV", L"2018"});
    std::printf("CHECK dxil raw: succeeded=%d message=%s\n", raw.succeeded, raw.messages.c_str());
    failures += raw.succeeded ? 1 : 0;
    // A compile error comes back with DXC's message.
    const effects::CompiledShader broken = compiler.Compile("float4 VS() : SV_Position { return undefinedThing; }", entry, effects::ShaderTarget::Spirv);
    std::printf("CHECK error: succeeded=%d message=%s\n", broken.succeeded, broken.messages.c_str());
    failures += (broken.succeeded || broken.messages.find("undefinedThing") == std::string::npos) ? 1 : 0;
    // Preprocessor errors in fxc's form; #include fails without a handler (even for a file that exists).
    const faf_posix::gal::PreprocessedEffect error =
      faf_posix::gal::PreprocessEffect("float a;\n#error stop here\n", "/effects/broken.fx", {});
    std::printf("CHECK #error: succeeded=%d errors=%s", error.succeeded, error.errors.c_str());
    failures += (error.succeeded || error.errors.rfind("/effects/broken.fx(2,2): error: stop here", 0U) != 0U) ? 1 : 0;
    const faf_posix::gal::PreprocessedEffect include =
      faf_posix::gal::PreprocessEffect("#include \"/etc/hosts\"\nfloat a;\n", "/effects/include.fx", {});
    std::printf("CHECK #include: succeeded=%d errors=%s", include.succeeded, include.errors.c_str());
    failures += include.succeeded ? 1 : 0;
    // Defines, with an empty value, and the #line name.
    const faf_posix::gal::PreprocessedEffect defines = faf_posix::gal::PreprocessEffect(
      "#if defined(EMPTY) && FOO == 3\nfloat ok = FOO;\n#endif\n", "/effects/defines.fx", {{"FOO", "3"}, {"EMPTY", ""}}
    );
    std::printf("CHECK defines: succeeded=%d text=[%s]\n", defines.succeeded, defines.text.c_str());
    failures += (!defines.succeeded || defines.text.find("float ok = 3;") == std::string::npos ||
                 defines.text.find("#line 1 \"/effects/defines.fx\"") == std::string::npos)
      ? 1
      : 0;
    // A redefinition is a warning: success, with the warning and its note in fxc's form.
    const faf_posix::gal::PreprocessedEffect redefined =
      faf_posix::gal::PreprocessEffect("#define FOO 4\nfloat x = FOO;\n", "/effects/redefined.fx", {{"FOO", "3"}});
    std::printf("CHECK redefinition: succeeded=%d errors=%s", redefined.succeeded, redefined.errors.c_str());
    failures += (!redefined.succeeded || redefined.errors.find("/effects/redefined.fx(1,9): warning:") == std::string::npos ||
                 redefined.text.find("float x = 4;") == std::string::npos)
      ? 1
      : 0;
    std::printf("CHECK front end: %d failure(s)\n", failures);
    return failures;
  }
} // namespace

int main(const int argc, char** const argv)
{
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <compat|-> <out dir> <label> <effect.fx>...\n", argv[0]);
    return 2;
  }
  static StderrLog log;

  effects::ShaderCompiler& compiler = effects::ShaderCompiler::Instance();
  std::printf("DXC available=%d version=[%s] error=[%s]\n", compiler.Available(), compiler.Version().c_str(), compiler.LoadError().c_str());
  std::printf("shader cache directory: [%s]\n", effects::ShaderCacheDirectory().c_str());
  if (!compiler.Available()) {
    return 1;
  }

  const std::string outRoot = std::string(argv[2]) + "/" + argv[3];
  ::mkdir(argv[2], 0755);
  ::mkdir(outRoot.c_str(), 0755);

  int failures = 0;
  if (std::string(argv[1]) == "-") {
    // No effects: the front-end checks and the GPU-driven programs.
    failures += RunFrontEndChecks();
    failures += RunGpuPrograms(outRoot);
    QuickExit(failures != 0 ? 1 : 0);
  }

  const std::string compat = ReadFile(argv[1]);
  const bool parallel = std::getenv("FAF_TEST_PARALLEL") != nullptr && std::getenv("FAF_TEST_PARALLEL")[0] == '1';
  Totals totals;
  const Clock::time_point start = Clock::now();
  std::vector<std::shared_ptr<effects::EffectProgram>> keep;
  for (int index = 4; index < argc; ++index) {
    std::shared_ptr<effects::EffectProgram> background;
    RunEffect(compat, argv[index], outRoot, totals, parallel ? &background : nullptr);
    keep.push_back(background);
  }
  const double wall = Ms(start, Clock::now());
  const char* const cacheDirectory = std::getenv("FAF_SHADER_CACHE_DIR");
  std::printf(
    "TOTAL %s effects=%d effect-failures=%d techniques=%d passes=%d entries=%d compiled=%d failed=%d warned=%d linked=%d "
    "unwritten-inputs=%d reflect-mismatch=%d cache-mismatch=%d preprocess=%.0fms compile=%.0fms cached=%.0fms wall=%.0fms "
    "cache-files=%d parallel=%d\n",
    argv[3], totals.effects, totals.effectFailures, totals.techniques, totals.passes, totals.entries, totals.compiled, totals.failed,
    totals.warnings, totals.linkedModules, totals.unwrittenInputs, totals.reflectMismatches, totals.cacheMismatches, totals.preprocessMs,
    totals.compileMs, totals.cachedMs, wall, cacheDirectory != nullptr ? CountFiles(cacheDirectory) : -1, parallel ? 1 : 0
  );
  failures = totals.effectFailures + totals.failed + totals.reflectMismatches + totals.cacheMismatches;
  QuickExit(failures != 0 ? 1 : 0);
}
