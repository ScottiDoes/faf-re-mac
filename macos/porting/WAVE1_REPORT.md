# Wave 1 report: macOS native port, compile pass

Verifier pass run after every wave-1 porting agent had finished. I reconfigured the build, ran the full census, audited the working tree against `src/platform/posix/README.md`, and wrote this report. Nothing was committed.

## 1. Headline numbers

| | Before wave 1 | After wave 1 |
|---|---|---|
| Failing engine TUs (wx, net, core-sys, render, audio-movie, sim-game) | **157** | **8** |
| Error diagnostics | **770** (excluding the known hidden ones) | **33** in the ninja log (41 with `-ferror-limit=0`; X87Math.cpp alone has 27) |
| Wild Magic Foundation TUs failing | 32 (not counted in the 157) | **0** of 386 |
| zlib TUs failing | 0 | 0 of 12 (already up to date; this run did not recompile them) |

- **Build size:** `faf_engine` has 951 TUs: 921 of the 958 entries in `src/sources.cmake`, plus 30 files in `platform/posix`. Of these, **943 compile**.
- **What happened to the 157:**
  - **138** now compile.
  - **11** are excluded by package `exclusions.d/*.cmake`, each replaced by a stub or port under `src/platform/posix/stubs/`.
  - **8** still fail.
- **Census files:**
  - Log: `build/macos/census-wave1.log`.
  - Per-package lists: regenerated as `build/macos/census/<package>.txt`. `wm3.txt` is new; empty files mean nothing fails.
  - Pre-wave lists: kept in `build/macos/census/before-wave1/`.
- **Command:**
  ```
  cmake -S macos -B build/macos
  ninja -C build/macos -k 0 -j 10 faf_engine Wm3Foundation faf_zlib
  ```
  Ninja exited 1. Every `faf_engine` TU was compiled in this run, so nothing was skipped as up to date.

## 2. What the verifier changed

- **`macos/CMakeLists.txt`:** added the wx stand-in include directory `"${FAF_SRC}/platform/posix/wx"` to `faf_engine`'s `BEFORE PUBLIC` include list. It comes right after `platform/posix/include` and before every other include directory. No other lead request was applied (section 6).
- **Census:** regenerated the files in `build/macos/census/`, copying the old ones to `build/macos/census/before-wave1/`.
- **This report.**

The include path alone unblocked about 60 TUs across five packages: every file that stopped on `wx/defs.h`.

## 3. Per-package status

| Package | Failing before | Failing after | Still failing (first cause) |
|---|---|---|---|
| wx | 45 | **0** | none. WinApp.cpp and WinMain.cpp are excluded and replaced by `stubs/wx/WinAppPosix.cpp` and `stubs/wx/WinMainPosix.cpp`. |
| net | 17 | **1** | `moho/net/CLobby.cpp`: boost 1.34 `std::unary_function`, which libc++ removed. **Verified:** with `-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION` it compiles cleanly. |
| core-sys | 20 | **1** | `legacy/math/X87Math.cpp`: 27 × undeclared `msvc8_x87_*`. The declarations sit behind `#if defined(_M_X64)` (X87Math.cpp:9). This is the determinism track. |
| render | 38 | **2** | `gpg/gal/backends/vulkan/DeviceVulkan.cpp:197-204` needs `BITMAPFILEHEADER`, `BITMAPINFOHEADER` and `BI_RGB`.<br>`moho/render/d3d/CD3DDevice.cpp:1829` needs `AddFontMemResourceEx`.<br>Both are gdi32, which has no owner. |
| audio-movie | 9 | **0** | none. 3 TUs are excluded and stubbed. |
| sim-game | 28 | **4** | **`moho/sim/Sim.cpp:10143`:** `sscanf_s`, in the `sim_Gravity` console command.<br>**`moho/console/CConCommand.cpp:1471`:** `std::exception("...")` uses an MSVC-only constructor.<br>**`moho/path/PathTables.cpp:1783`:** `GPG_PREREGISTER_INIT` is not declared, because the `StaticInitPhase.h` include is under `#ifdef _WIN32` at :35. **Verified:** it compiles once the header is included.<br>**`moho/misc/StartupHelpers.cpp`:** see the next row. |
| (StartupHelpers detail) | | | Three layers, each found by probing past the one before:<br>1. `<dsound.h>` not found at :31.<br>2. Then `DeviceD3D10.hpp` and `DeviceD3D9.hpp` at :38-39 pull in `d3d10.h`.<br>3. Then `std::ofstream(const wchar_t*)` at :6136 (an MSVC extension; this is `USER_SavePreferences`).<br>After these three it compiles. |
| wm3 | 32 | **0** | none |

Error counts with `-ferror-limit=0`:

| File | Errors |
|---|---|
| X87Math.cpp | 27 |
| DeviceVulkan.cpp | 3 |
| CLobby.cpp | 3 (one cause) |
| PathTables.cpp | 3 (one cause) |
| Sim.cpp | 2 (one cause) |
| CConCommand.cpp | 1 |
| CD3DDevice.cpp | 1 |
| StartupHelpers.cpp | 1 visible + 2 hidden layers |

**Requests the agents filed during the wave and later satisfied by other agents.** These files now compile:
- `_dupenv_s`, `getenv_s`, `GetCommandLineA`, `SHGetFolderPathA`, `CreateProcessA` and `ShellExecuteExW` (Misc).
- `GetForegroundWindow` and `GetClientRect` (Window).
- `GetProcessMemoryInfo` (Memory).
- `RtlCaptureStackBackTrace` and `GetModuleHandleExA` (Misc and Modules).
- `CreateFileA` and `FreeLibrary` (Files and Modules).
- `OutputDebugStringA`.
- `_PC_24` and `_MCW_PC`.
- The shared `FAF_POSIX_STUB` macro.

### Link probe (informational, not a build step)

I linked the 943 compiled objects with `libWm3Foundation.a` and `libfaf_zlib.a` in a scratch directory to see how far the build is from linking. There were 349 undefined symbols. Matching them by name against the 8 failing TUs accounts for all but 6:

| Count | Source |
|---|---|
| 168 | Sim.cpp |
| 60 | StartupHelpers.cpp |
| 42 | CConCommand.cpp |
| 37 | X87Math.cpp and the x87 helpers |
| 20 | DeviceVulkan.cpp |
| 12 | CD3DDevice.cpp |
| 3 | PathTables.cpp |
| 1 | CLobby.cpp |

The 6 not explained by the failing TUs:
- **`___RTtypeid`:** declared and called in `gpg/core/reflection/Reflection.cpp:185,860`. This is the MSVC runtime's polymorphic typeid and needs a `FAF_POSIX` branch (Itanium: the type_info pointer at `vtable[-1]`). It is a **link blocker on the reflection/serialization path**.
- **`_FSGetCatalogInfo`, `_FSRefMakePath`, `_GetCurrentProcess`, `_GetProcessBundleLocation`:** from Wm3System.cpp. The fix is to link `-framework CoreServices -framework ApplicationServices`; the symbols exist in the SDK's .tbd files.
- **`_main`:** the headless runner provides it.

Once the 8 TUs compile, the engine is close to linking. The name heuristic is approximate, so treat these counts as indicative. The linker also warns about an auto-linked `psapi.lib` from a `#pragma comment(lib)`; this is harmless but noisy.

## 4. Rule audit

I covered:
- `git diff`: 38 modified engine files.
- `git status --porcelain`: untracked `src/platform/posix/**`, `macos/**`, `dependencies/patches/*_faf_macos.patch`, `dependencies/boost_1_34_1/` and `dependencies/berkeley-softfloat-3/`.
- The working trees of the dependency submodules.

### 4.1 What passed

- **Guards on upstream engine files.** Every hunk in the 38 modified files takes one of three forms:
  - an `#if defined(FAF_POSIX)` ... `#else` ... `#endif` block;
  - a `#if !defined(FAF_POSIX)` exclusion;
  - an existing `_WIN32` / `_M_X64` condition widened with `|| defined(FAF_POSIX)`.

  For Windows (`_WIN32`, `_MSC_VER`, `_M_X64`) the preprocessed result is unchanged in every file. One textual exception is behaviour-identical (4.2, I-2).
- **No sim math changes.** No float-math semantics were edited anywhere.
  - Three sim-adjacent integer edits restore Windows 32-bit `long` semantics on POSIX:
    - `legacy/containers/HashMap.h` `hash_value` (int32);
    - `gpg/core/containers/ReadArchive.cpp` `ReadLong`/`ReadULong` (4-byte lanes);
    - `lua/LuaObject.cpp` `tonumber(s, base)` (MSVC 32-bit `strtoul` semantics: saturation and negation checked).
  - `Global.cpp` makes POSIX take the existing x64 allocator path, which matches the x64 reference build.
  - The D3DX9 matrix re-implementation (`stubs/render/gal/D3DXMathPosix.cpp`) is reached only through `gal::Math` from `MathReflection.cpp` (`VEC_Mul`, `VMatrix4::Load*`), CEfxEmitter, Shadow and CWldTerrainDecal. A grep found **no sim caller** of `VEC_Mul` or `VMatrix4::Load*` outside render and UI.
  - The scalar fallback in `moho/math/VMatrix4.h` (where `MOHO_USE_SSE2` is 0 on arm64) uses the same left-to-right operation order as the SSE path, and `-ffp-contract=off` is set, so it gives the same bits.
- **No 64-bit atomics on reinterpret-cast 32-bit fields.**
  - The `compat/Sync.h` overloads for `long` and `unsigned long` (`FAF_INTERLOCKED_32`, :25-61) are 32-bit `std::atomic<int32_t>` operations, so every engine `reinterpret_cast<volatile long*>(&field32)` call through Interlocked is correct.
  - I reviewed all 56 `reinterpret_cast<...long*>` sites in engine code. Every one that reaches Interlocked is 32-bit. `CrashReport.cpp:1015` sits inside the dialog code that POSIX excludes. `Platform.h:44-45` is listed in 4.3.
  - The remaining 64-bit atomics are on the code's own 64-bit fields:
    - `WinCompatInternal.h:168` (`std::atomic<long> refs`);
    - `LuaObject.cpp:13023`;
    - `Timer.cpp:119` (`InterlockedCompareExchange64` on a `LONGLONG`).
  - `UiRuntimeTypes.cpp:1607` now bit_casts through `int32_t`.
- **Windows sizes.** I compiled a probe with the engine's exact flags.
  - At the Windows x64 sizes: `BYTE`/`CHAR` 1, `WORD`/`SHORT`/`ATOM` 2, `DWORD`/`LONG`/`ULONG`/`BOOL`/`INT`/`UINT`/`HRESULT`/`LCID`/`COLORREF`/`u_long`/`errno_t` 4, `LONGLONG`/`DWORD64`/`*_PTR`/`SIZE_T`/`HANDLE`/`LPARAM`/`WPARAM`/`LRESULT`/`LARGE_INTEGER`/`FILETIME` 8, `SYSTEMTIME`/`GUID`/`RECT` 16, `WIN32_FIND_DATAA` 320.
  - Deliberate in-memory-only deviations:
    - `WCHAR` is 4, because it is `wchar_t` (README rule 3).
    - `SOCKET` is 4, a POSIX fd (8 on Windows x64).
    - `CRITICAL_SECTION` is 48 (40 on Windows).
    - `WIN32_FIND_DATAW` is 1140 (592 on Windows).
- **Dependencies.**
  - **WildMagic3p8:** verified that the working tree equals `git archive HEAD` + `wildmagic3p8_faf_required.patch` + `wildmagic3p8_faf_macos.patch` (`diff -r` is empty).
    - Every line the macOS patch changes is one of these:
      - a `this->` qualification;
      - the `__APPLE__`-only `WM3_BIG_ENDIAN` / `WM3_INSTANTIATE_BEFORE` edits in `Wm3Platforms.h`;
      - the new `WM3_INSTANTIATE_BEFORE` branch in GridGraph2.cpp, whose `#else` keeps the original order;
      - `LCPSolver::LCPSolver(...)` changed to `LCPSolver(...)`.
    - None of these changes behaviour.
  - **boost_1_34_1:** verified that the tree equals the local tarball (`dependencies/.downloads`) + `boost_1_34_1_faf_required.patch` + `boost_1_34_1_faf_macos.patch`, apart from a `.git` directory.
    - All macOS hunks are `FAF_POSIX`-guarded except `boost/thread/xtime.hpp`. There, the push condition gains `|| __clang__` and the pop condition drops `&& _MSC_VER`. Neither change has an effect on clang-cl, which defines `_MSC_VER`.
  - **zlib:** the working-tree diff is exactly `zlib_1_2_3_faf_required.patch`.
- **Stubs log.** Every stub file in `stubs/` that fails or does nothing logs through `FAF_POSIX_STUB`. audio-movie still uses its local `FAF_AUDIO_MOVIE_STUB`, which prints the same tag. The six files with no stub log are real ports or wrappers, not stubs.

### 4.2 Violations

| # | Severity | Where | Rule | Problem |
|---|---|---|---|---|
| V-1 | **Medium** | `dependencies/patches/boost_1_34_1_faf_macos.patch:22-28` → `dependencies/boost_1_34_1/boost/detail/sp_counted_base.hpp:30`; counts at `sp_counted_base_w32.hpp:44-45` | 2 (Windows sizes) | See below. |
| V-2 | Low | `src/platform/posix/WinCompatMisc.cpp:1530` (`RegOpenKeyExA`), `:1543` (`RegOpenKeyExW`) | 6 (stubs log once) | The registry stub returns `ERROR_FILE_NOT_FOUND` silently: no `FAF_POSIX_STUB` line. `RegCreateKeyEx` fails the same way. The Windows return value is right; only the log is missing. |

**V-1 in detail.** The patch picks boost's Win32 counted base on POSIX. Its comment says this keeps "the Windows layout `{ vptr, long, long }`", but `long` is 8 bytes on macOS, so it does not.
- **Measured with the engine flags:**
  - `sizeof(boost::detail::sp_counted_base)` is 24 (16 on Windows x64).
  - `sp_counted_impl_p::px_` sits at +0x18 (+0x10 on Windows x64).
- **Engine views that assume `{vptr, int32 use, int32 weak, px@+0x10}`:**
  - `boost::SpCountedImplStorage` in `gpg/core/utils/BoostWrappers.h:1287-1293`;
  - `SharedCountControl` in `gpg/core/algorithms/Cluster.cpp:30-35`;
  - the dispose helpers in `moho/render/d3d/CD3DPrimBatcher.cpp:137` and `CD3DTextureBatcher.cpp:71`.
- **What breaks.** `Cluster.cpp:2665` builds the HaStar cluster-cache control block in the engine layout (via `BoostWrappers.cpp:1637`). On that block:
  - boost's `weak_release` would decrement `px`;
  - `add_ref_lock` reads `use | weak<<32` as one 64-bit `long`, so a `weak_ptr::lock()` on it would never match its 32-bit CAS result.

  Today only engine code touches that block (LP64-10: a leak), but the layout the patch claims is not the layout it produces.
- **Fix (core-sys):** in the `FAF_POSIX` branch, use a FAF copy of `sp_counted_base_w32` with `boost::int32_t` (`LONG`) counts. Interlocked on `LONG` is 32-bit. Also change `Cluster.cpp:33-34` to `volatile LONG`.

Informational items (not violations):
- **I-1.** `gpg/core/reflection/StaticInitPhase.h:47`: the comment names `platform/posix/StaticInitPreRegister.cpp`; the file is `platform/posix/startup/StaticInitPreRegister.cpp`.
- **I-2.** `gpg/gal/backends/vulkan/DeviceVulkan.cpp:1578-1581`: the Windows branch was restructured. The D3DPreprocess result's `string_view` is now a named local, `preprocessedText`, instead of being built inline in the `TranslateEffectSource` call. Behaviour is identical (the `preprocessed` blob is still in scope), but the Windows text changed.
- **I-3.** `BoostWrappers.cpp:1492,2155` delete pointers to the now-incomplete D3D9/D3D10 effect types on POSIX (`-Wdelete-incomplete`). core-sys's comment says these lanes never run on POSIX. That is true while no Direct3D object exists; the warning should stay visible.
- **I-4.** `dependencies/berkeley-softfloat-3/` is a fresh git clone (18:03). `macos/deps.cmake` declares an `EXCLUDE_FROM_ALL` test-oracle target for it, and it is not built by the census targets or linked into the engine. The determinism research said downloading it needs the user's approval, so the lead should confirm that approval was given.

### 4.3 Upstream branches that matter for milestone 1

These are pre-existing upstream code, not this wave's edits. The POSIX build defines neither `_WIN32` nor `_MSC_VER`, so these upstream `#else` branches are now live on the sim path:

| Where | Problem |
|---|---|
| `moho/misc/StatItem.cpp:478,487,504,524,562,1555,2202`<br>`moho/unit/core/Unit.cpp:1579,6823`<br>`moho/entity/EntityDb.cpp:225`<br>`moho/entity/Entity.cpp:346`<br>`moho/sim/CArmyStats.cpp:102,116` | `#if defined(_WIN32)` / `_MSC_VER` atomics fall back to **non-atomic** read-modify-write while the sim's parallel jobs run (`SimWorkerPool`). This is LP64 audit A1. **Fix:** widen these guards with `\|\| defined(FAF_POSIX)`; the compat Interlocked overloads are 32-bit-correct. |
| `moho/sim/WldSessionInfo.cpp:164` | `CreateSemaphoreW(..., LONG_MAX, ...)`. The compiler confirms "changes value from 9223372036854775807 to -1", so creation fails (LP64-05). |
| `moho/collision/CColPrimitiveBase.cpp:155` | `std::fmin`/`std::fmax` give the opposite sign of zero on arm64 (determinism research). |
| 109 `lrintf`/`lrint` sites | They return a 64-bit `long`; NaN and out-of-range values differ (LP64-01). |
| `moho/sim/Sim.cpp:6020` | `"%I64"` (LP64-13). |
| `moho/sim/CWldMap.cpp:1401` | UTF-32 preview name (LP64-04). |
| `moho/sim/CWldSessionLoaderImpl.cpp:34` (`/interlocked`)<br>`moho/path/PathTables.cpp:1208` (`/genpath`) | Gated on `_WIN32`, although `GetCommandLineA` now exists on POSIX. |
| `platform/Platform.h:44-45` | `LODWORD`/`HIDWORD` read through `unsigned long*`, 8 bytes (LP64-15). |
| `legacy/containers/Vector.h:1695` | `[[msvc::no_unique_address]]` is ignored by Apple clang (831 warnings). `msvc8::vector<T, false>` is 32 bytes on macOS against 24 on Windows x64 (measured). This is in-memory only; use `[[no_unique_address]]` under `FAF_POSIX` if any size assumption appears. |
| `include/immintrin.h:127` (lead-owned) | Maps MXCSR.FTZ to FPCR.FZ. ARM FZ also flushes denormal **inputs**. `SimWorkerPool` copies the main thread's control bits to workers (`SimWorkerPool.cpp:112`). Nothing sets FTZ today, but this should assert or refuse. |

## 5. Build-log noise worth fixing

- 825 × `'BOOST_HAS_PTHREADS' macro redefined`. The `-DBOOST_HAS_PTHREADS` in `macos/CMakeLists.txt` collides with boost's `posix_features.hpp`.
- 831 × `unknown attribute 'msvc::no_unique_address'` (see 4.3).
- 30 × `-Wundefined-bool-conversion` (`this` null checks in AI navigators) and 2 × `-Winvalid-noreturn` (`CAnimationManipulator.cpp:174`, `CEconomyEvent.cpp:302`). These are upstream code shared with the clang-cl build, not port issues.

## 6. Lead requests not applied

As instructed, I applied only the wx include path. Everything below is unapplied, unless marked satisfied by another agent. The computed task text cut off in the middle of the lp64 lead requests; I took the rest of that list from `macos/docs/LP64_AUDIT.md` §Lead requests.

### Lead-owned shared files

| Owner | Request |
|---|---|
| wx, core-sys, render, audio-movie | **`macos/tu.sh`:** the fallback for files not in `compile_commands.json` pastes the resolved path (`/Volumes/SSD Programs/...`, with a space) into the command string unquoted, which breaks `shlex.split`. **Fix:** swap the file argument after splitting, or `shlex.quote(src)`. |
| net | **`macos/CMakeLists.txt`:** define `_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION`, or extend the boost macOS patch. This **unblocks CLobby.cpp (verified)**. |
| audio-movie | **`macos/CMakeLists.txt`:** add `src/platform/posix/stubs/audio-movie/include` to the BEFORE include dirs, or move `dsound.h`, `xact3.h` and `PosixWaveFormat.h` into `platform/posix/include/`. This is layer 1 of StartupHelpers.cpp. |
| determinism | **`macos/CMakeLists.txt`:** add `-fmath-errno` to `faf_codegen`. Measured: libc++ `std::sin(float)` and `std::pow(float, float)` are otherwise constant-folded, and `pow(x, 2)` becomes `x*x`. |
| core-sys | **`macos/CMakeLists.txt`:** drop `BOOST_HAS_PTHREADS`, or define it empty (825 warnings). |
| render | **`macos/exclusions.cmake`:** narrow `^gpg/gal/backends/(d3d9\|d3d10\|d3d11\|d3d12\|dxgi)/` so that `dxgi/DxgiFormats.cpp`, `d3d11/D3D11VertexLayouts.cpp` and `d3d11/EffectSourceD3D11.cpp` build in place. Then delete the three wrappers in `stubs/render/gal/`. |
| core-sys | **`startup/StaticInitPreRegister.cpp`:** call `faf_posixInitArguments(argc, argv)` from the first initializer, which can take `(int, char**, char**)` on Darwin. Otherwise engine static initializers see a null `__argv`. |
| wm3 | Make the macOS build apply or verify `wildmagic3p8_faf_macos.patch` on top of the required patch. Today it is applied by hand, and `cmake/FafDependencies.cmake` lists only faf_required. Optionally update the comment at `macos/deps.cmake:28-29`. |
| lp64 | **`WinCompat.h`:** choose the `lrintf`/`lrint`/`lround` strategy (LP64-01): a force-included shim, or a guarded macro at the 109 sites. |
| lp64 | **`LayoutAsserts.h`:** add a `FAF_FORMAT_ASSERT(...)` that stays on for pointer-free file and wire formats: `SSaveGameFileHeader`, `SPacketBody*`, zip records, `SScaHeader`/`SScmFile`, `PacketLogRecord`, `SWorldMapFileHeader`. |
| lp64 (optional) | Put `format(printf)` attributes on `gpg::Logf`, `Warnf`, `Debugf`, `STR_Printf` and `Sim::Logf` under `FAF_POSIX`, and keep the raw-I/O probe as a CI check. |
| determinism | **`include/immintrin.h`:** `_mm_setcsr` must never set FPCR.FZ on sim threads; assert or refuse. |
| core-sys | **`WinCompat.h`:** decide whether `DWORD_PTR`/`ULONG_PTR`/`SIZE_T` (`unsigned long`) and `DWORD64`/`ULONGLONG` (`unsigned long long`) should be one type, as on Windows x64. Today this needs per-API overloads. |
| wx | Done by this pass: reconfiguring picked up `exclusions.d/wx.cmake` and the new stubs. |

### Decisions for the lead

- **gdi32 has no owner.** `GetDC`/`ReleaseDC`, `CreateDIBSection`, `BITMAPINFO`, `BITMAPFILEHEADER`, `BITMAPINFOHEADER`, `BITMAPV5HEADER`, `BI_RGB`, `HBITMAP`, `DeleteObject`, `GetObject`, `FillRect`, `GetStockObject` and `AddFontMemResourceEx` are unowned. The wx agent offered to put them in the Window area. This blocks DeviceVulkan.cpp and CD3DDevice.cpp.
- **Headless renderer.** Either a null `gal::Device` (recommended by render), or guards around `CWldTerrainRes::Finalize`, `NotifyMapChange` and the render entry points. `CWldSession::DoBeat` reaches `gal::Texture::Create` without a null check.
- **Static-init position of WinApp.cpp's statics.** They now run from the stubs directory, after every engine object, which changes the order. The alternative is to keep `WinApp.cpp` in the build with its body under `#if !defined(FAF_POSIX)`.
- **New `src/platform/posix/math/` area plus owner.** It would hold `X87Core.h`, `X87Kernels.cpp`, `X87Emu.cpp` (the 27 `msvc8_x87_*`), `CrtMath.cpp` (faf_crtmath with trace mode) and `MsvcRand.cpp`.
- **A1 owner.** Non-atomic stat fallbacks (section 4.3).
- **User approvals** (determinism, all optional): AMD LibM (BSD-3) as the base for UCRT 22000 clones; Berkeley SoftFloat 3e and/or mpmath as test oracles. SoftFloat is already cloned (I-4), so confirm. Hardware verification needs the user's Windows PC, Intel and AMD.

### Requests for other packages (still open)

- **core-sys:**
  - X87Math.cpp: the one-line guard `#if defined(_M_X64) || defined(FAF_POSIX)` around the `msvc8_x87_*` declarations (X87Math.cpp:9).
  - `sscanf_s` in compat Strings (Sim.cpp).
  - `Platform.h` `LODWORD`/`HIDWORD` through `std::uint32_t*` (raised by net, core-sys and lp64).
  - LP64-07 (`String.cpp` UTF-8 encode/decode).
  - LP64-11 (Lua `os.clock`).
  - The boost V-1 fix.
  - `__RTtypeid` (link probe).
- **sim-game:**
  - LP64-05 (`WldSessionInfo.cpp:164`, `0x7FFFFFFF`).
  - LP64-04, LP64-10 and LP64-13.
  - The PathTables.cpp include.
  - `CColPrimitiveBase.cpp:155` fmin/fmax.
  - StartupHelpers.cpp:38-39 D3D includes (render's request).
  - LP64-03, the save header as `char16_t` (not needed for milestone 1).
- **Lua owner:** non-finite number formatting must match the UCRT (`inf`, `-inf`, `nan`, `-nan(ind)`).
- **audio-movie:** switch `FAF_AUDIO_MOVIE_STUB` to the shared `FAF_POSIX_STUB`, which now exists.
- **Headless runner:** call `SetSse2Mode(0)`, as `CScApp::Init` does.
- **CI** (once linked):
  - an objdump scan for FMA in sim objects;
  - an `nm` check that the libm/`rand` symbols are defined in the executable;
  - a startup self-test (FPCR, saturation, fused-vs-unfused probe, the `rand` sequence, the x87 known answers).

### Satisfied during the wave

| Request (from) | Now in |
|---|---|
| `_dupenv_s` (net) | Misc |
| `CreateProcessA` and `ShellExecuteExW` (net) | Misc |
| `getenv_s`, `GetCommandLineA`, `SHGetFolderPathA`, `RtlCaptureStackBackTrace` (render) | Misc |
| `GetModuleHandleExA` (render) | Modules |
| `GetForegroundWindow` and `GetClientRect` (render) | Window |
| `GetProcessMemoryInfo` (render) | Memory |
| `CreateFileA`, `FreeLibrary`, `OutputDebugStringA` (audio-movie) | Files, Modules, Misc |
| `FAF_POSIX_STUB` (audio-movie) | Errors |
| `_PC_24` and `_MCW_PC` (determinism) | Misc |
| D3D includes in BoostWrappers.cpp and WxRuntimeTypes.cpp (render) | done by core-sys and wx |
| LP64-02, LP64-06 and LP64-08 | done by core-sys |
| LP64-14 | done by wx |

## 7. Recommended wave-2 work packages

### W2-1 sim-game (first)

Goal: every sim TU compiles, and the sim path takes the Windows behaviour.

1. **The four remaining TUs:**
   - `Sim.cpp:10143`: `sscanf_s`, once core-sys has it, or a guarded `std::sscanf`.
   - `CConCommand.cpp:1471`: a `FAF_POSIX` branch throwing `std::runtime_error` with the same text.
   - `PathTables.cpp:35`: include `StaticInitPhase.h` for `FAF_POSIX`.
   - `StartupHelpers.cpp`:
     - :31, via the lead's dsound include path;
     - :38-39, guard the D3D10 and D3D9 device includes;
     - :6136, open the preferences file with a UTF-8 path on POSIX.
2. **Atomics (A1):** widen the 13 non-atomic stat fallbacks listed in section 4.3 to `_WIN32 || FAF_POSIX` / `_MSC_VER || FAF_POSIX`.
3. **LP64 and determinism on sim sites:**
   - `WldSessionInfo.cpp:164`;
   - `CColPrimitiveBase.cpp:155`;
   - the 109 `lrintf` sites, following the lead's LP64-01 decision;
   - `Sim.cpp:6020` (`%I64`);
   - `CWldMap.cpp:1401`;
   - `Cluster.cpp:33-34`, as `LONG`;
   - the `/interlocked` and `/genpath` gates, which should take the Windows branch now that `GetCommandLineA` exists.
4. **Headless replay runner.**
   - Write `main()` and a headless `IWinApp` subclass through `WIN_AppExecute`.
   - The init sequence:
     1. `faf_posixInitArguments`;
     2. `APP_InitializeIdentity`;
     3. `AppInitCommonServices`;
     4. `USER_LoadPreferences`;
     5. `/lua/userInit.lua`;
     6. `USER_EnsureDocumentDirectories`;
     7. `SetSse2Mode(0)`;
     8. `VCR_SetupReplaySession`;
     9. `WLD_BeginSession`.
   - Each frame calls `WLD_Frame` and `UserFrame`. Run with `/nosound /nomovie /exitongameover /framerate N`.
   - Skip `CreateDevice`, `REN_Init` and `REN_Frame`, combined with the lead's null-device decision.

### W2-2 core-sys follow-ups

- X87Math.cpp guard (one line), so the TU compiles.
- `sscanf_s` in compat Strings.
- `__RTtypeid` POSIX branch in Reflection.cpp (link blocker).
- boost V-1: 32-bit counted base.
- `Platform.h` `LODWORD`/`HIDWORD`.
- LP64-07 and LP64-11.
- The registry stub log (V-2).
- Optional: `[[no_unique_address]]` under `FAF_POSIX` in `Vector.h`.

### W2-3 determinism / x87 math (needs the new math area)

- The 27 `msvc8_x87_*` symbols on a software x87:
  - exact precision control;
  - the PcRoundCore fast paths;
  - double-double transcendentals with Intel's π reduction.
- `faf_crtmath`: a UCRT clone with trace mode.
- MSVC `rand`/`srand`.
- Lua NaN/inf spelling.
- An FPCR guard.
- Plan and verification are in `macos/docs/DETERMINISM.md`.

### W2-4 link milestone (lead + core-sys)

- Link `faf_engine` into the runner, with `-framework CoreServices -framework ApplicationServices` for Wm3System.
- Then work through the undefined symbols that remain once the 8 TUs compile; the probe suggests only `__RTtypeid`.
- Confirm static-init order, and that `StaticInitPreRegister.cpp` is linked first.

### W2-5 render

- Implement the gdi32 bits once the owner decision is made.
- Write the null `gal::Device`.
- Narrow the d3d11/dxgi exclusion.

### W2-6 net

- Nothing beyond the lead's `_LIBCPP` define; CLobby then compiles.
- Later: a lobby wire-format test against a Windows peer, for the `u_long` macro.
