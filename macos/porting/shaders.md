# Porting notes: package "shaders" (milestone 2)

HLSL effects to SPIR-V on macOS with the Vulkan SDK's DXC, for the Vulkan backend on
MoltenVK/KosmicKrisp.

Owned: `src/platform/posix/gal/HlslPreprocess.cpp`, `src/platform/posix/dxc/**`,
`macos/dxc.cmake`, `src/platform/posix/stubs/render/gal/ShaderCompilerPosix.cpp`,
FAF_POSIX-guarded edits in `src/gpg/gal/effects/**`, `macos/tests/shaders/**`.

## Status

Every effect the game loads (FAF's `effects.nx2`: 11 effects, 371 techniques, 431 passes,
344 entry points) preprocesses, translates and compiles to SPIR-V on macOS with no failure;
every module passes `spirv-val`, converts to MSL with MoltenVK's converter and compiles
with the Metal runtime compiler. The shader cache resolves to
`~/Library/Application Support/FAForever/ShaderCache` and round-trips. The headless replay
still passes with the bridge linked into `faf_replay` (27890640: 0 mismatches, 79x).

## What is where

| File | What |
|---|---|
| `src/platform/posix/dxc/DxcBridge.h` | The bridge's interface: plain C++ (no Win32, no DXC types). `Load()`, `Compile(source, wchar_t arguments)` -> object / `-P` text / messages, `Widen()` (UTF-8 -> the UTF-32 wchar_t DXC takes off Windows) |
| `src/platform/posix/dxc/DxcBridge.cpp` | `dlopen`s `libdxcompiler.dylib`, `DxcCreateInstance` -> `IDxcUtils` + `IDxcCompiler3` through DXC's own `dxcapi.h`/`WinAdapter.h` (`CComPtr`). Built as the OBJECT library `faf_dxc_bridge` without `WinCompat.h` (`macos/dxc.cmake`), linked into `faf_replay` and `faf_game` |
| `src/platform/posix/stubs/render/gal/ShaderCompilerPosix.cpp` | `gpg::gal::effects::ShaderCompiler` on POSIX (compiled in place of `effects/ShaderCompiler.cpp`) |
| `src/platform/posix/gal/HlslPreprocess.cpp` | `faf_posix::gal::PreprocessEffect` (D3DPreprocess's job; contract `HlslPreprocess.h`) |
| `src/gpg/gal/effects/EffectRuntime.cpp` | one FAF_POSIX edit: `FAF_EFFECT_DUMP_DIR` joins with `/` (it goes to `fopen`, not through the compat layer, and `\` is a file name character on POSIX) |
| `macos/tests/shaders/` | `run_effects.sh` (the whole test below), `effects_test.cpp` (engine-linked harness), `metal_compile.mm` (Metal runtime compile of MSL), `bridge_smoke.cpp` + `build_smoke.sh` (bridge alone, no engine) |

### Loading DXC

Search order (task spec): `$FAF_DXC_DIR/libdxcompiler.dylib`, then
`FAF_VULKAN_SDK_DIR "/lib/"` (the SDK the build was configured with; compile definition
from `macos/dxc.cmake`), then next to the executable (`_NSGetExecutablePath` + `realpath`),
then the bare name (`DYLD_LIBRARY_PATH` / default search). A library that exists but does
not load (wrong architecture, signing) is reported with its `dlerror`. Loaded once
(`std::call_once`), `RTLD_NOW | RTLD_LOCAL`, never closed. A shipped app needs
`libdxcompiler.dylib` next to the executable (or `FAF_DXC_DIR`).

Errors, as the Windows front end words them: "libdxcompiler.dylib not found in FAF_DXC_DIR,
the Vulkan SDK (<sdk>/lib) or next to the executable", "libdxcompiler.dylib does not provide
IDxcCompiler3", "DxcCreateInstance failed on this thread", "IDxcCompiler3::Compile failed
(0x........)". `DeviceVulkan::Setup` throws `LoadError()` as on Windows.

`Version()` (part of every shader cache key) is `dxc <major>.<minor> (<commits> <hash>)`,
e.g. `dxc 1.9 (5512 01b62ad4)`: Windows records `dxc 1.9 signed|unsigned`; here the commit
is in it as well so another DXC build never reuses old SPIR-V. One log line on load:
`effects: dxc 1.9 (5512 01b62ad4) loaded from <path>`.

### ShaderCompilerPosix: same behaviour as ShaderCompiler.cpp

- `Compile` (SPIR-V): the Windows argument list unchanged (`-E -T vs_6_0|ps_6_0 -HV 2018
  -Zpr -O3 -D FAF_*_STAGE=1 -Wno-conversion -spirv -fvk-use-dx-layout -fspv-reflect
  -fspv-target-env=vulkan1.1 -fvk-t-shift 0 0 -fvk-s-shift 64 0 -fvk-b-shift 0 0
  -fvk-bind-globals 0 1 [-fvk-invert-y]` + extra), no source name (DXC's `hlsl.hlsl`), no
  include handler, messages = DXC_OUT_ERRORS (warnings on success), "\nno object produced".
- `CompileRaw`: `-E -T <profile> -O3` + extra, as Windows (GpuDrivenVulkan's programs).
- Per-thread `IDxcUtils`/`IDxcCompiler3` (thread_local in the bridge), as Windows.
- DXIL target (only the excluded Direct3D 12 backend asks for it): `Compile(Dxil)` and a
  `CompileRaw` without `-spirv` fail with "DXIL is not available on this platform (no
  dxil.dll signer); only the SPIR-V target compiles".
- `ReflectInputs`: Windows reads a DXIL input signature with `ID3D12ShaderReflection`
  (`EffectRuntime` calls it for DXIL only). On POSIX it reads SPIR-V instead
  (`effects::ReflectSpirvInterface`): one input per location (semantic upper case, index,
  component type, mask from the component count, system values flagged); empty for
  anything that is not SPIR-V. The Vulkan backend gets its stage interface from
  `SpirvReflect` directly (`DeviceVulkan::InterfaceOf`), so nothing else needed DXIL
  reflection.
- Arguments vs DXC versions: the SDK's DXC 1.9.0.5512 (commit 01b62ad4) accepts every
  argument the Windows front end and GpuDrivenVulkan pass (Windows pins v1.9.2609); none
  rejected, no new warnings besides the effects' own `-Wunused-value`.

### HlslPreprocess: D3DPreprocess on DXC's preprocessor

`IDxcCompiler3::Compile` with `-P -flegacy-macro-expansion -D name=value ...` (the effect's
macros in order: DeviceVulkan's injected ones plus the context's), no include handler, the
text from DXC_OUT_HLSL. What `d3d11::TranslateEffectSource` and the effect layer assume of
D3DPreprocess's text, and what DXC gives:

| Assumption | DXC `-P` |
|---|---|
| Every macro expanded everywhere, incl. technique/pass blocks and annotations (`technique` -> `technique10`, `compile vs_2_0 F()` -> `compile vs_4_0 F()`, `MipFilter = LINEAR` -> `Filter = MIN_MAG_MIP_LINEAR`, `AlphaState(p)` -> `SetBlendState(...)`) | yes (clang preprocessor) |
| Conditionals resolved, comments removed, string literals intact | yes |
| No directive lines other than `#line` / pass-through `#pragma` (the translator drops every line starting with `#`) | `#line N "<name>"` only |
| Token pasting expands macro operands first (fxc) | `-flegacy-macro-expansion` (DXC's "fxc behavior" switch). No game effect differs with or without it (checked all 25 files) |
| `#include` fails (no ID3DInclude) | no include handler: "'x' file not found" even for files that exist |
| Whitespace | free: the translator and parsers are whitespace-agnostic (`\s*`, Trim) |

The engine's source names are VFS paths (`/effects/mesh.fx`) that DXC's option parser
would take for `/`-options, so DXC gets a fixed name (`faf_effect_source.fx`) and the real
one is put back into `#line` and the diagnostics. Diagnostics come back in fxc's form,
`<name>(<line>,<col>): error: <message>`, one per line, without clang's source excerpts
(locations in the defines keep DXC's `<built-in>`). Warnings (e.g. a macro redefined with
another value) succeed and come back in `errors`. Non-UTF-8 bytes (FA's Latin-1 `°` in
comments) pass: they are in comments.

Cross-checks: the number of techniques the effect layer parses equals the `technique`
declarations in the sources (e.g. FAF terrain.fx 68, mesh.fx 180); no preprocessor warning
on any game effect; preprocessing takes 0.4-3.8 ms per effect (mesh.fx, 301 KB).

### Shader cache

`effects::ShaderCacheDirectory()` needs no change: `SHGetFolderPathA(CSIDL_LOCAL_APPDATA)`
(compat Misc) gives `C:\Users\<user>\Library\Application Support` and the effect layer
appends `\FAForever\ShaderCache\`; compat Files maps that to
`~/Library/Application Support/FAForever/ShaderCache/` for `SHCreateDirectoryExA`,
`CreateFileA`, `ReadFile`/`WriteFile` and `MoveFileExA(MOVEFILE_REPLACE_EXISTING)` (the
atomic temp-file rename). `FAF_SHADER_CACHE_DIR` takes a POSIX path (the effect layer
appends `\`, which compat Files turns into `/`). The logged directory is in that Windows
form. Cache file format unchanged (`FAFSHDR1`, key, size, SPIR-V).

## Tests and results

`macos/tests/shaders/run_effects.sh` (needs `build/m2-shaders` built:
`cmake -S macos -B build/m2-shaders -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C
build/m2-shaders faf_replay`; ~2 min; output in `build/m2-shaders-tests`). It links
`effects_test` from faf_replay's object list (StaticInitPreRegister first: the engine's
initializers need it) with the runner swapped for the test, and exits through
`std::_Exit` as the runner does.

Per effect, exactly as `DeviceVulkan::CreateEffect` + `CD3DEffect::InitEffectFromFile`:
`d3d10states.compat` + `<effect>.fx` -> `PreprocessEffect` (DeviceVulkan's 21 injected
macros) -> `TranslateEffectSource(text, true, {})` -> `EffectProgram(SPIR-V, original)` ->
`CompileAll` (every entry of every pass) -> each module stripped
(`StripSpirvReflection`) and each pass's pixel shader linked to its vertex shader
(`PatchSpirvInputLocations`, as `DeviceVulkan::ModuleFor`) -> `spirv-val --target-env
vulkan1.1 --scalar-block-layout` on raw, stripped and linked modules -> `spirv-cross --msl`
-> MoltenVK's `MoltenVKShaderConverter` to MSL -> Metal runtime compiler
(`newLibraryWithSource`, MSL 3.0, Apple M2 Max). Then a second `EffectProgram` over the same
text must come entirely from the cache with identical code; the FAF set also runs with a
warm cache and with every effect prepared on the background shader workers while the main
thread compiles (per-thread DXC objects under contention; identical modules required).

Effect sets: what `init_faf.lua` mounts first (FAF `gamedata/effects.nx2` = the game's
effects), FA's own `effects.scd` (shadowed in the game; informational), and the three
effects in `faforever.faf` (not mounted by init_faf.lua; checked anyway).

| Set | Effects | Techniques | Passes | Entry points | Compiled | Failed | Linked pixel modules | Cold compile | From cache |
|---|---|---|---|---|---|---|---|---|---|
| FAF (game) | 11 | 371 | 431 | 344 | 344 | 0 | 283 | 7.8-8.4 s serial, 2.9 s with the background workers (19.9 s / 7.4 s while other agents' builds loaded the machine) | 40 ms (all 344) |
| FA vanilla | 11 | 266 | 290 | 257 | 248 | 9 (water2.fx, not ours) | 196 | 3.4 s | 48 ms |
| faforever.faf | 3 | 161 | 201 | 174 | 174 | 0 | 135 | 4.3 s | 23 ms |
| GPU-driven programs (`moho/render/*Shader.h`, GpuDrivenVulkan's arguments) | 7 programs | | | 12 (5 VS, 5 PS, 2 CS) | 12 | 0 | | 1-9 ms each | |

FAF set per effect (cold, serial): mesh.fx 180 entries 5.3 s; terrain.fx 79 entries 2.1 s;
water2.fx 11 / 57 ms; frame.fx 20 / 68 ms; particle.fx 16 / 80 ms; primbatcher.fx 14 /
45 ms; cartographic.fx 6 / 28 ms; sky.fx 8 / 30 ms; ui.fx 6 / 19 ms; vision.fx 3 / 7 ms;
range.fx 1 / 2 ms. Translation (TranslateEffectSource, regex-heavy, not this package)
takes 15-200 ms per effect; preprocessing 0.4-3.8 ms; the EffectProgram build 1-19 ms.

Validation: 1472 stripped modules, 1185 linked pixel modules, 1460 raw modules (all sets
incl. the warm/parallel copies): 0 `spirv-val` failures, 0 `spirv-cross --msl` failures;
1392 distinct modules through MoltenVKShaderConverter: 1392 MSL, 1392 compile with Metal,
0 failures. No pixel input is left unwritten by its pass's vertex shader. ReflectInputs
agrees with the interface reflection on every vertex shader. 180 of the 344 FAF entries
compile with DXC's `-Wunused-value` warning (the effects' own code; same arguments as
Windows).

Front-end checks (`effects_test -`): DXIL target and DXIL CompileRaw fail with the message;
a compile error returns DXC's text; `#error` -> `/effects/broken.fx(2,2): error: stop here`;
`#include "/etc/hosts"` fails (`fatal error: '/etc/hosts' file not found`); defines incl.
an empty value; `#line 1 "/effects/defines.fx"`; a redefinition is a warning in fxc form
(`/effects/redefined.fx(1,9): warning: 'FOO' macro redefined [-Wmacro-redefined]`, its note
`<built-in>(65,9): note: previous definition is here`: DXC files the -D defines under
`<built-in>`).

Default cache location: `ui.fx` twice without `FAF_SHADER_CACHE_DIR`: 6 files written to
`~/Library/Application Support/FAForever/ShaderCache`, second run compiles nothing.
`FAF_EFFECT_DUMP_DIR` writes `<dir>/ui.fx.{original.fx,translated.fx,hlsl}`.

Every touched engine TU compiles with `macos/tu.sh` (HlslPreprocess.cpp,
ShaderCompilerPosix.cpp, EffectRuntime.cpp); the bridge builds as `faf_dxc_bridge` in
`build/m2-shaders`; `faf_replay` links and passes the milestone-1 replay from that tree
(`FAF_REPLAY_EXE=build/m2-shaders/faf_replay SPEED=50 macos/run_replay.sh`:
`FAF_REPLAY_RESULT replay=27890640 beats=9716 mismatches=0 exit=end-of-replay`, 79x; again
after the last changes, on a loaded machine: `beats=9713 mismatches=0 exit=end-of-replay`,
30x. The last beat varies run to run, 9710-9716 in earlier reports).

## Failures found that are not this package's

- FA vanilla `water2.fx` (all 9 entries): `float waterLerp = clamp(waterDepth, waterLerp.x,
  waterLerp.y);` declares a local that shadows the global `float2 waterLerp` in its own
  initializer. fxc resolved the initializer to the global; DXC uses C++ scoping (the local
  is in scope) -> "vector swizzle 'y' is out of bounds". Identical with DXC on Windows
  (D3D12 and Vulkan use the same generated HLSL and arguments). The game does not load this
  file (FAF's water2.fx, mounted first, is fixed). A fix would belong in the shared effect
  layer (ModernHlsl: rename a local whose initializer reads a global of the same name).
- Process exit through `exit()` aborts in `moho::DISK_ResetWatch` (an `atexit` handler from
  CDiskWatch.cpp locks a mutex already destroyed): the test exits with `std::_Exit` as the
  headless runner does; `faf_game` will need the same (or the handler fixed).

## Open items / notes

- No offline Metal toolchain is installed (`xcrun metal` asks for a download), hence the
  runtime compiler check. MoltenVK's pipeline-time conversion uses its own options
  (resource bindings, argument buffers), so the converter run is close to, not identical
  with, what MoltenVK compiles in the game.
- KosmicKrisp (Mesa NIR) has no offline check here; the modules are valid SPIR-V 1.3
  for Vulkan 1.1 with scalar block layout.
- `macos/exclusions.d/render.cmake` and `macos/porting/render.md` still describe
  ShaderCompilerPosix.cpp as "reports itself unavailable" (render package files).
