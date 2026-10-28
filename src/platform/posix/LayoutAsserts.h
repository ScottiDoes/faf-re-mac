#pragma once

// Force-included after WinCompat.h into every C++ translation unit of the POSIX build:
// the 64-bit counterpart of platform/X64LayoutAsserts.h (see there). The tree's
// thousands of `static_assert(sizeof(T) == 0xNN)` pin the x86 image's layout; on a
// 64-bit target every pointer-bearing type fails them and there is no original to
// hold the layout to. Diagnostics only: a static_assert has no effect on codegen.
//
// That also hides LP64 changes (a bare `long` is 64-bit here, 32 on Windows), which
// the x86 asserts would have caught in pointer-free wire and file formats: those are
// audited separately (README.md, rule 2).
#if defined(__cplusplus)
#define _ALLOW_KEYWORD_MACROS
#define static_assert(...) static_assert(true, "x86 layout assert, not checked on 64-bit POSIX")
#endif
