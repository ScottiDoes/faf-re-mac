# core-sys: Win32 OS services for the engine core

Package scope: `src/gpg/core/**`, `src/moho/core/**`, `src/lua/**`, `src/legacy/**` (not
`legacy/math/X87Math*`), the boost.thread sources, and the compat areas Errors, Sync,
Threads, Time, Files, Strings, Memory, Modules and Misc (`src/platform/posix/compat/<Area>.h`
and `WinCompat<Area>.cpp`), plus the private `src/platform/posix/WinCompatInternal.h`.

## Status

- Every translation unit of the package compiles with `macos/tu.sh`, except two:
  - `gpg/core/reflection/Reflection.cpp` reaches `platform/WxWidgets.h` (`wx/defs.h`)
    through `moho/ui/UiRuntimeTypes.h`. That belongs to the wx package.
  - `legacy/math/X87Math.cpp` is on the determinism track and out of scope.
- No translation unit is excluded, and no stubs exist under `stubs/core-sys`.
- Across the whole engine (932 TUs, lead exclusions applied), 855 compile. None of the
  77 that fail stops on a core-sys symbol. Their first errors are missing headers (`wx/defs.h` ×65,
  `wrl/client.h`, `dsound.h`, `dxgi1_4.h`, libavcodec), GDI types (BITMAP*, render/Window
  areas), `msvc8_x87_*`, `std::unary_function` in boost.algorithm, one
  `GPG_PREREGISTER_INIT` include (sim-game), and XactClock's PE import walk (already excluded
  by audio-movie).
- Behaviour tests: a standalone program built from the nine `WinCompat*.cpp` files passes
  all of its checks, repeatedly. It covers threads and events, mutexes, semaphores, APCs,
  critical sections, SRW locks, condition variables, one-time init, files, virtual memory,
  heaps, time, code pages, printf, modules, tool help, environment, CreateProcess, known
  folders and topology. A second program replaces `operator new` and shows that no compat
  path the engine allocator calls ever reaches it. Both programs live in the session
  scratchpad (`coresys/cs_test.cpp`, `coresys/cs_alloc_test.cpp`) and are not in the tree.

## The compat areas

All handles live in one handle table (`WinCompatInternal.h`), with values 4, 8, 12 and so
on, as on Windows. Each slot holds a refcounted kernel object. `CloseHandle` and
`DuplicateHandle` work on every kind. `GetCurrentProcess()` returns -1 and
`GetCurrentThread()` returns -2; both pseudo handles are accepted wherever a handle is.

- **Errors**: a thread-local `GetLastError`/`SetLastError` holding Windows codes. errno
  values are mapped, never passed through. `FormatMessageA/W` returns kernel32's English
  text for system codes (Win32 and Winsock), handles `FROM_STRING` inserts,
  `ALLOCATE_BUFFER` and `MAX_WIDTH_MASK`, and fails unknown ids with
  `ERROR_MR_MID_NOT_FOUND`. **`FAF_POSIX_STUB(what)` is defined here**: it logs once per
  call site to stderr.
- **Sync**: events (manual/auto, named), mutexes (recursive, owned, abandoned on thread
  exit giving `WAIT_ABANDONED`), and semaphores (`ERROR_TOO_MANY_POSTS`).
  - `WaitForSingleObject(Ex)`, `WaitForMultipleObjects(Ex)` (wait-any and wait-all),
    `SignalObjectAndWait` and `MsgWaitForMultipleObjects(Ex)` take timeouts and return the
    `WAIT_*` codes.
  - Waits are satisfied when the object is signalled, as in the NT kernel. An auto-reset
    event set twice releases two waiters, and a released mutex goes to its oldest waiter.
  - Alertable waits run queued APCs and return `WAIT_IO_COMPLETION`.
  - Named objects are shared within the process only (`ERROR_ALREADY_EXISTS`; an existing
    named mutex ignores `bInitialOwner`).
  - `CRITICAL_SECTION` is recursive, built on `os_unfair_lock`. It keeps `OwningThread`
    (a thread id) and `RecursionCount`.
  - `SRWLOCK`, `CONDITION_VARIABLE` and `INIT_ONCE` are pointer-sized and zero-initialised,
    and never allocate (address-hashed parking lot).
  - `faf_posixSetThreadMessageEvent(HANDLE)` lets the window layer give a thread a
    "message queue" event for MsgWait.
- **Threads**: `CreateThread` and `_beginthreadex`/`_beginthread` run on pthreads.
  - Stacks are at least 2 MB (twice Windows' default reserve, for 64-bit frames).
  - `CREATE_SUSPENDED` holds the thread at a start gate until `ResumeThread`.
  - `SuspendThread`/`ResumeThread` use Mach `thread_suspend`/`thread_resume`.
  - Thread handles are waitable and report `GetExitCodeThread`/`STILL_ACTIVE`.
  - Every thread gets a Win32 id (nonzero, a multiple of 4, never reused) and its kernel
    object on its first call into the layer, so `OpenThread(id)` finds any thread whose id
    was handed out. Thread.cpp's `THREAD_InvokeAsync`/`Wait` to the main thread depends on
    this.
  - `QueueUserAPC`, `Sleep`/`SleepEx` and `SwitchToThread` are implemented. TLS uses
    pthread keys; `TlsGetValue` clears the last error.
  - `GetProcessAffinityMask` covers every CPU. `GetLogicalProcessorInformationEx` comes
    from sysctl: performance cores first with EfficiencyClass 1, then efficiency cores,
    with one NUMA node, package and group, and L2 caches per perf level.
  - Priority, affinity, ideal processor and group affinity are recorded and returned, but
    not applied (macOS has no CPU affinity).
- **Time**:
  - `QueryPerformanceCounter` is `mach_absolute_time`; the frequency comes from the
    timebase (24 MHz on Apple Silicon).
  - `GetTickCount(64)` and `timeGetTime` count CLOCK_MONOTONIC milliseconds (they include
    sleep, like Windows).
  - FILETIME/SYSTEMTIME conversions use proleptic Gregorian arithmetic. Local conversions
    use the current zone bias.
  - Also covered: DOS date/time, `CompareFileTime`, and the CRT `_time64` family and the
    MSVC-order `localtime_s`/`gmtime_s`.
- **Files**: paths go in with either separator, and any `X:` drive is the POSIX root.
  - Paths come out in Windows form, `C:\Users\...`. That covers the current directory,
    module path, `GetFullPathName`, temp path, known folders and `_getcwd`.
  - `_getdrive()` returns 3. The engine's own drive arithmetic in StartupHelpers needs
    system paths with a drive.
  - On a case-sensitive volume, a lookup that fails is retried component by component,
    ignoring case.
  - The handle-based functions are implemented: `CreateFile` with every disposition and
    Windows' error codes (`ERROR_FILE_EXISTS`, `ERROR_ALREADY_EXISTS` for OPEN_ALWAYS and
    CREATE_ALWAYS, `ERROR_PATH_NOT_FOUND` vs `ERROR_FILE_NOT_FOUND`), `DELETE_ON_CLOSE`,
    directory handles, `"nul"`, Read/WriteFile (positional with OVERLAPPED), SetFilePointer
    (Ex), GetFileSize(Ex), SetEndOfFile, GetFileType, GetFileTime, SetFileTime and
    GetFileInformationByHandle.
  - Name-based functions are implemented: `FindFirst/NextFile` and `_findfirst64` (DOS
    wildcard rules, case-insensitive, entries in **NTFS index order**: `.`, `..`, then
    upper-cased UTF-16 ordinal; APFS readdir order is hash order), attributes, create/remove directory, delete,
    move/replace/copy, and `SHFileOperation` (FO_DELETE with wildcards; FOF_ALLOWUNDO
    moves to `~/.Trash`), plus `SHCreateDirectoryEx` and `PathFileExists`.
  - Also covered: temp file names, std handles, file mappings (`MapViewOfFile` and named
    mappings within the process), and the secure-CRT fopen family with MSVC mode strings.
    `_lseek` and `_filelength` keep 32-bit results.
- **Strings**:
  - The ANSI and OEM code page is UTF-8 (`GetACP() == CP_UTF8`). 1252 and 28591 are
    converted by table.
  - `MultiByteToWideChar`/`WideCharToMultiByte` follow Windows' size and error
    conventions; wchar_t is UTF-32, and surrogate pairs are combined.
  - The secure CRT (`strcpy_s`, `strncpy_s` with `_TRUNCATE`, `sprintf_s`, `_snprintf_s`,
    `vsnprintf_s`, `swprintf_s`, the C++ array overloads, ...) keeps MSVC's truncation and
    return rules. Violations call the `_set_invalid_parameter_handler` handler.
  - `_snprintf`/`_vsnprintf`/`_snwprintf` are now real functions (they had been
    `#define`d to snprintf/swprintf): -1 and no terminator when the output does not fit.
  - MSVC format strings are translated: wide `%s` means wide text, `%S` the other width,
    and `%I64`, `%I32` and `%I` are supported.
  - Also covered: number conversions and `_towlower_l` and the other `_l` variants (null
    locale).
- **Memory**:
  - `VirtualAlloc` reserves with mmap at 64 KB granularity and commits with mprotect.
    Decommitted pages read as zero once committed again.
  - `VirtualQuery` reports the layer's regions exactly, and everything else from the Mach
    VM map (MEM_IMAGE, MEM_MAPPED or MEM_PRIVATE; MEM_COMMIT, MEM_RESERVE or MEM_FREE; real
    protections). `__PAGEZERO` shows as reserved.
  - `VirtualProtect`, `IsBadReadPtr`/`IsBadWritePtr` and `FlushInstructionCache` are
    implemented.
  - Heaps are malloc zones; the process heap is the default zone. `HeapSize` returns -1
    for a pointer from another heap.
  - Also covered: Global/Local blocks, `GlobalMemoryStatusEx`, `GetProcessMemoryInfo`,
    `memcpy_s`/`memmove_s` and `_aligned_*`.
- **Modules**: an HMODULE is the Mach-O header address.
  - `GetModuleHandle(Ex)` finds a module by name or (FROM_ADDRESS) by an address inside
    it. `GetModuleFileName` follows the Vista truncation rules.
  - `LoadLibrary` uses dlopen. Windows DLL names fail with `ERROR_MOD_NOT_FOUND`.
    `GetProcAddress` uses dlsym.
  - Tool help snapshots list dyld images, and the threads the layer knows by their Win32
    ids.
- **Misc**:
  - Processes: `CreateProcessA/W` uses posix_spawn, splits the command line by
    CommandLineToArgvW rules, and returns a waitable process handle with an exit code.
    `TerminateProcess` and `ExitProcess` are implemented.
  - Environment variables are looked up case-insensitively. The Windows profile variables
    (`USERPROFILE`, `APPDATA`, `TEMP`, ...) are derived when unset.
  - Also covered: `ExpandEnvironmentStrings`, `GetCommandLineA/W` (argv re-quoted),
    `CommandLineToArgvW`, `__argc`/`__argv`/`__wargv`, `getenv_s`/`_dupenv_s`/`_putenv`,
    `_popen`.
  - System information: `GetSystemInfo` reports ARM64 and the host page size (16 KB).
    `GetVersionEx` reports 10.0.19045 NT. NUMA queries report one node.
  - `SHGetFolderPath` maps PERSONAL to ~/Documents, (LOCAL_)APPDATA to
    ~/Library/Application Support, COMMON_APPDATA to /Users/Shared, PROFILE to ~, and so
    on.
  - `ShellExecute(Ex)` opens a file or URL with /usr/bin/open.
  - `_controlfp`/`_control87`/`_clearfp`/`_statusfp`/`_fpreset` map rounding and denormal
    flush onto FPCR/FPSR.
  - Also covered: the CRT hooks (`_purecall`, `_callnewh`, `_set_purecall_handler`,
    `_CrtSetDbgFlag`, ...) and `RtlCaptureStackBackTrace` (execinfo).
  - dbghelp `SymFromAddr`/`SymGetSymFromAddr64` use dladdr and demangle. The unsuffixed
    names map to the 64-bit API, as x64 dbghelp.h does.

**Engine-allocator safety.** `operator new` is the engine's allocator (Global.cpp). That
allocator calls `VirtualAlloc`, `InitOnceExecuteOnce`, SRW locks, `GetEnvironmentVariableA`,
`GetCommandLineW`, the heap functions and `GetCurrentThreadId` while it starts up or holds
its locks. On those paths the compat code never calls `operator new`: kernel objects, the
region map and the argv/command-line copies all come from the system heap
(`SystemAllocator`).

### Stubbed: log once with FAF_POSIX_STUB and fail the Windows way

These are diagnostics and UI paths only:

- `GetThreadContext`/`SetThreadContext` (no x86 context exists).
- `RtlCaptureContext` (zero-filled, with the caller's PC and FP).
- `StackWalk64` and `MiniDumpWriteDump`. `SymGetLineFromAddr64` returns FALSE.
- `SetUnhandledExceptionFilter` and `AddVectoredExceptionHandler`: recorded, never called,
  because signals are not translated into SEH.
- `TerminateThread`.
- `ReadDirectoryChangesW`: `ERROR_NOT_SUPPORTED`, for the hot-reload disk watcher.
- `MoveFileEx(DELAY_UNTIL_REBOOT)`.
- Registry: keys never exist, and RegCreateKeyEx fails.
- Process tokens and privileges.
- `SetConsoleCtrlHandler`.
- `CreateProcess` std-handle redirection and `CREATE_SUSPENDED`.
- `ShellExecute` verbs other than open.
- `FormatMessage(FROM_HMODULE)`.

## Guarded edits to engine files (owned)

Each POSIX path is under `FAF_POSIX`; the Windows code is unchanged.

- `gpg/core/utils/Global.cpp`:
  - The x64 allocator path (`small_pool` plus the system heap) is also the POSIX path:
    five `#if defined(_M_X64)` gain `|| defined(FAF_POSIX)`. Without this the arm64 build
    compiled the x86 page allocator, whose 4-byte size classes cannot hold 8-byte links.
  - `ImageIsLargeAddressAware`, `ReserveAddressSpaceBelow4Gb` and `BeginHighMemoryMode`
    (PE header walk) and the `.CRT$XLB` TLS-callback registration are Windows-only. On
    macOS, `__PAGEZERO` already keeps the low 4 GB unmapped, and the x64 path's
    `thread_local` exit guards do the thread-exit work.
- `gpg/core/utils/Sync.h`: POSIX takes the Win32 branches, which these compat objects now
  provide. The old `!_WIN32` branch made `FastMutex` non-recursive and lacked
  `init_critical_section`.
- `gpg/core/utils/BoostWrappers.cpp`: the D3D9/D3D10 backend headers are not included.
  Those backends are not built, so their shared_ptr dispose lanes never run (forward
  declarations suffice).
- `gpg/core/diagnostics/CrashReport.cpp`: the PE debug-directory (PDB identity) block and
  the Win32 report dialog are Windows-only. On POSIX the report path goes to stderr.
- `gpg/core/diagnostics/FrameSampler.cpp`: there is no `__try` on POSIX.
  `GetThreadContext` never yields a context there, so the walk does not run.
- `gpg/core/containers/ReadArchive.cpp` (rule 2): the binary archive's `long`/`ulong`
  lanes are 4 bytes. They are read into an int32/uint32 and widened (sign-extended),
  instead of filling only the low half of a 64-bit `long`.
- `legacy/containers/HashMap.h` (rule 2): VC8 `hash_value` runs on a 32-bit `long`.
  Callers narrow `key ^ _HASH_SEED` into it, which wraps on Windows. With a 64-bit long
  every hash, and so bucket and iteration order, differed. POSIX uses `std::int32_t`.
- `lua/LuaObject.cpp`:
  - `tonumber(s, base)` (rule 2): MSVC's `strtoul` is 32-bit (it saturates at 0xFFFFFFFF
    and negates in 32 bits). POSIX uses a 32-bit equivalent (`StrToULong32`).
  - `__iob_func()` returns Darwin's contiguous `stdin`/`stdout`/`stderr` (`__sF[3]`), so
    `&__iob_func()[2]` is stderr. It had been a zero-filled placeholder array.
- boost 1.34.1 (`dependencies/patches/boost_1_34_1_faf_macos.patch` regenerated):
  - `libs/thread/src/thread.cpp` and `tss.cpp`: `std::auto_ptr` becomes `std::unique_ptr`
    (libc++ C++20 has no auto_ptr).
  - `boost/detail/sp_counted_base.hpp`: on FAF_POSIX, shared_ptr counts use the **w32**
    variant `{vptr, long, long}` with Interlocked operations. About 35 engine call sites
    (Cluster.cpp, Entity, CWldSession, terrain, ...) adjust `use_count_`/`weak_count_` in
    place with `InterlockedIncrement`. The default macOS variant would be `pt`, which adds
    a pthread mutex (another layout) and races against those raw atomics.
  - `boost/detail/interlocked.hpp`: FAF_POSIX takes the `BOOST_USE_WINDOWS_H` route
    (compat/Sync.h).

## Determinism notes

- Directory enumeration order now matches NTFS (see Files). It decides mount and mod scan
  order, and through that load order. Without it, APFS would hand back hash order.

- `_controlfp(_PC_24, _MCW_PC)` (x87 precision) is recorded but has no effect on ARM, as
  the x64 CRT ignores it. `FAF_POSIX_TODO(determinism)` is in compat/Misc.h; the X87Math
  track covers it.
- Text-mode CRT streams (`"r"`/`"w"` without `b`) do no CRLF translation on POSIX. Engine
  writes in text mode are logs only. Lua's `luaL_loadfile` and `io.open` read with plain
  `std::fopen(..., "r")`, so a CRLF file keeps its `\r`, which matters only inside `[[long
  strings]]`. Check this if a sim script ever comes from such a path instead of the VFS.
- None of the clocks (`QueryPerformanceCounter`, `GetTickCount`, ...) may feed the sim.
  They have different rates than on Windows.

## Open issues

- Share modes in `CreateFile` are not enforced (no `ERROR_SHARING_VIOLATION`).
- There is no cross-process namespace for named objects (only WinApp's single-instance
  mutex is affected, and its result is ignored).
- Directory change notification (CDiskWatch hot reload) needs an FSEvents/kqueue
  implementation.
- `OutputDebugString` writes to stderr only with a debugger attached or `FAF_DEBUG_OUTPUT=1`.
  `gpg::LogContext` sends warnings there when no log target is attached.
- Diagnostic tools (`ProbeWatch` int3 patching, `FrameSampler` sampling, crash minidumps)
  are inert on POSIX.

## Lead requests

1. **`__argc`/`__argv` before engine static init.** They are filled by an initializer in
   `WinCompatMisc.cpp` (Darwin passes argc/argv to image initializers). That object links
   after the engine's, so engine initializers run first and would see `nullptr`.
   `GetCommandLineA/W` initializes lazily and is not affected. Please call
   `faf_posixInitArguments(argc, argv)` (declared in compat/Misc.h, idempotent) from
   `startup/StaticInitPreRegister.cpp`'s initializer (give it the `(int, char**, char**)`
   signature) or from `main`.
2. **`DWORD_PTR`/`ULONG_PTR`/`SIZE_T` vs `DWORD64`/`ULONGLONG`.** In WinCompat.h these are
   distinct types on macOS (`unsigned long` vs `unsigned long long`) but one type on
   Windows x64. x64 code that passes a `DWORD_PTR*` where a `PDWORD64` is expected does not
   compile. I added a `DWORD_PTR*` overload of `SymGetSymFromAddr64` for WinApp.cpp; decide
   whether WinCompat.h should change instead.
3. **`src/platform/Platform.h`**: `LODWORD`/`HIDWORD` cast through `unsigned long*`, which
   is 64-bit on macOS (rule 2). They should use `std::uint32_t*`.
4. **`-DBOOST_HAS_PTHREADS`** (macos/CMakeLists.txt) collides with boost's own
   `#define BOOST_HAS_PTHREADS` (posix_features.hpp), giving "macro redefined" in every TU.
   Define it empty (`BOOST_HAS_PTHREADS=`) or drop it, since boost sets it on macOS.
5. **`macos/tu.sh`** breaks for files that are not in compile_commands.json when the real
   path contains a space (`/Volumes/SSD Programs/...`): the path goes into the command
   string unquoted. audio-movie reported this too.
6. For sim-game (rule 2): `moho/sim/WldSessionInfo.cpp` creates its queue semaphore with
   `CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr)`. `LONG_MAX` is 2^63-1 on macOS and
   truncates to -1 in the `LONG` parameter, so the call fails with
   `ERROR_INVALID_PARAMETER` (Windows semantics). It should use `MAXLONG`/`0x7FFFFFFF`.
7. For sim-game: `moho/path/PathTables.cpp` uses `GPG_PREREGISTER_INIT` without including
   `gpg/core/reflection/StaticInitPhase.h`, which nothing pulls in on POSIX.
8. For wx: `gpg/core/reflection/Reflection.cpp` (core-sys) only waits on `wx/defs.h` via
   `moho/ui/UiRuntimeTypes.h`.
9. FYI, other packages: `FAF_POSIX_STUB` now exists (compat/Errors.h), so audio-movie's
   local macro can switch to it. `MsgWaitForMultipleObjects(Ex)` is in Sync and
   `ShellExecute(Ex)` in Misc (Window defers to them). `SW_*` in Misc are `#ifndef`-guarded
   behind Window.h's.
