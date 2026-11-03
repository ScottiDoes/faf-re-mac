# POSIX platform layer (native macOS build)

The native Apple Silicon build (`macos/`) compiles the engine's translation units
against this directory instead of the Windows SDK. Milestone 1 (reached): a
headless native run of a FAF replay that matches every recorded checksum. Milestone 2: the
game drawn in a window (SDL3, Vulkan on MoltenVK/KosmicKrisp from the LunarG Vulkan SDK).

## Layout

| Path | What |
|---|---|
| `WinCompat.h` | Force-included into every C++ translation unit: Win32 types at their Windows sizes, common macros, then the area headers |
| `compat/<Area>.h`, `WinCompat<Area>.cpp` | One Win32 area each (Errors, Sync, Threads, Time, Files, Strings, Memory, Modules, Sockets, Window, Gdi, Misc): declarations and their POSIX implementations. `compat/Math.h` gives `std::lrint`/`lrintf` Windows' 32-bit `long` results |
| `include/` | Stand-ins for Windows SDK headers (`<windows.h>`, `<winsock2.h>`, ...) and the x86 intrinsic headers (`immintrin.h`: SSE/MMX on NEON; MXCSR.FTZ is recorded, never applied) |
| `wx/` | Header-only wxWidgets 2.4.2 stand-in (on the include path) |
| `math/` | Software x87 for the 27 `msvc8_x87_*` routines; `X87Types.h` is the contract |
| `crt/` | The C runtime pieces the sim depends on: UCRT-compatible math entry points (`faf_crtmath`), MSVC `rand` |
| `headless/` | Contracts between the headless runner and the subsystems it starts |
| `runner/` | The headless replay runner (`faf_replay`: `main()`); built into its own executable |
| `stubs/` | Stand-ins for subsystems whose real port comes later (renderer device, audio, app shell) |
| `startup/` | `StaticInitPreRegister.cpp`, the first object linked: command line, then the pre-register providers |
| `shell/` | The window system (SDL3): native windows behind the Window area's HWNDs and the wx stand-in, input, cursors, each window's CAMetalLayer. `Shell.h` is the contract |
| `gal/` | Renderer pieces for the Vulkan backend on macOS that are not stand-ins (the HLSL preprocessor: `HlslPreprocess.h` is the contract) |
| `dxc/` | The bridge to DXC (`libdxcompiler.dylib`), built without `WinCompat.h` (DXC's headers declare their own COM types; `macos/dxc.cmake`) |
| `game/` | The game executable (`faf_game`: `main()` in place of `WinMain`, `CScApp` with real windows); built into its own executable |

## Rules

1. **The simulation must stay bit-identical to Windows.** Never change float math
   semantics. No `-ffast-math`, no FMA, no substituting a libm call for an x87 path. Math
   that the Windows build routes through `legacy/math/X87Math*` (the `msvc8::` and
   `msvc8_x87_*` functions) or the static UCRT must be reproduced exactly, not
   approximated. If unsure, leave a `FAF_POSIX_TODO(determinism)` comment and report it.
2. **Windows sizes.** Win32 types (`DWORD`, `LONG`, `ULONG`, ...) are 32-bit as on
   Windows. A bare `long`/`unsigned long` in engine code is 64-bit on macOS. Where it is
   stored in a serialized or checksummed structure, sent over the network, or its size is
   assumed (`sizeof`, layout asserts, pointer/int casts), change it to the fixed-width type
   the Windows build has (`std::int32_t`/`std::uint32_t`), and note it.
3. **`wchar_t` is 32-bit on macOS** (16 on Windows). In-memory wide strings are fine.
   Anything that reads or writes wide text from files, the network, the registry or the
   replay/save formats must convert UTF-16 explicitly.
4. **Minimal, guarded edits to upstream files.** Prefer adding to this directory. When an
   engine file must change, keep the Windows code as is and add the POSIX path with
   `#if defined(FAF_POSIX)` ... `#else` ... `#endif`, smallest possible scope.
5. **Areas are owned.** Each porting task owns its listed source directories and the
   compat areas it is given. Add a missing Win32 declaration to the owned area header and
   implement it in the matching `.cpp`. If a symbol belongs to an area you do not own,
   report it instead of editing that area.
6. **Implement real behaviour** where the engine relies on it (files, threads, events,
   timers, sockets). A stub that returns failure is acceptable only for UI or diagnostics
   paths that a headless replay run never takes, and must log once with `FAF_POSIX_STUB`.
7. **Windows behaviour of return values and errors** wherever callers check them
   (`GetLastError()`, `WAIT_TIMEOUT`, `INVALID_HANDLE_VALUE`, ...).

## Leaving out or replacing a Windows-only translation unit

Each porting package keeps `macos/exclusions.d/<package>.cmake`:

- `list(APPEND FAF_MACOS_REPLACED_SOURCES "<regex>=><path under src/>")` when a POSIX
  stand-in replaces the TU. The stand-in compiles **at the original's position** in
  `src/sources.cmake`, so its static initializers keep their place in the init order.
  This is the default for anything that has static initializers.
- `list(APPEND FAF_MACOS_EXCLUDED_SOURCES "<regex>")` when nothing replaces it.

The regex matches the `src/sources.cmake` entry, e.g. `"^moho/app/WinApp\\.cpp$"`.
Every symbol other TUs use from a left-out file must still be defined somewhere.

## Building

```bash
cmake -S macos -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release   # once
ninja -C build/macos -k 0 faf_engine                                 # the engine objects
ninja -C build/macos faf_replay                                      # the headless runner
macos/tu.sh <path/to/file.cpp>                                       # one TU, no ninja
```

`build/macos/compile_commands.json` has every command. Paths may contain spaces
(`/Volumes/My Disk/...`): quote them.
