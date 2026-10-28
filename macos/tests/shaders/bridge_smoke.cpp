// Smoke test of the DXC bridge alone (no engine): load, preprocess, compile to SPIR-V.
// Build: macos/tests/shaders/build_smoke.sh
#include "DxcBridge.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
  using namespace faf_posix::dxc;
  const LoadStatus& status = Load();
  std::printf("loaded=%d path=%s version=%s error=%s\n", status.loaded, status.path.c_str(), status.version.c_str(), status.error.c_str());
  if (!status.loaded) {
    return 1;
  }
  int failures = 0;
  // -P with defines, comments, a function-like macro and token pasting.
  {
    const std::string source =
      "// comment\n#define technique technique10\n#define AlphaState(p) SetBlendState( p, float4(0,0,0,0), 0xFFFFFFFF );\n"
      "#define DECL(n) float4 n##Tile;\n/* block\n comment */ DECL(Lower)\n"
      "technique T < string a = \"x;//y\"; > { pass P0 { AlphaState(B) VertexShader = compile vs_2_0 VS(); } }\n";
    const CompileOutput out = Compile(source, {L"-P", L"-flegacy-macro-expansion", L"-D", L"vs_2_0=vs_4_0", L"faf_effect.fx"});
    std::printf("--- preprocess: ok=%d object=%zu text=%zu\n%s\n--- messages:\n%s\n", out.succeeded, out.object.size(), out.text.size(),
                out.text.c_str(), out.messages.c_str());
    failures += !out.succeeded;
  }
  // #include without a handler fails.
  {
    const CompileOutput out = Compile("#include \"other.fx\"\nfloat x;\n", {L"-P", L"faf_effect.fx"});
    std::printf("--- include: ok=%d text=%zu messages:\n%s\n", out.succeeded, out.text.size(), out.messages.c_str());
  }
  // #error
  {
    const CompileOutput out = Compile("\n#error broken effect\n", {L"-P", L"faf_effect.fx"});
    std::printf("--- error: ok=%d messages:\n%s\n", out.succeeded, out.messages.c_str());
  }
  // Compile to SPIR-V with the Windows front end's arguments.
  {
    const std::string source = "float4x4 M; float4 VS(float4 p : POSITION) : SV_Position { return mul(p, M); }\n";
    const CompileOutput out = Compile(source, {L"-E", L"VS", L"-T", L"vs_6_0", L"-HV", L"2018", L"-Zpr", L"-O3", L"-D", L"FAF_VERTEX_STAGE=1",
                                               L"-Wno-conversion", L"-spirv", L"-fvk-use-dx-layout", L"-fspv-reflect",
                                               L"-fspv-target-env=vulkan1.1", L"-fvk-t-shift", L"0", L"0", L"-fvk-s-shift", L"64", L"0",
                                               L"-fvk-b-shift", L"0", L"0", L"-fvk-bind-globals", L"0", L"1", L"-fvk-invert-y"});
    std::printf("--- spirv: ok=%d object=%zu magic=%08x messages:\n%s\n", out.succeeded, out.object.size(),
                out.object.size() >= 4 ? *reinterpret_cast<const unsigned*>(out.object.data()) : 0U, out.messages.c_str());
    failures += !(out.succeeded && out.object.size() > 4);
    if (argc > 1 && out.succeeded) {
      if (FILE* f = std::fopen(argv[1], "wb")) { std::fwrite(out.object.data(), 1, out.object.size(), f); std::fclose(f); }
    }
  }
  return failures;
}
