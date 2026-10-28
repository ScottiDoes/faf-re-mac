# Package wm3: Wild Magic 3.8 Foundation on Apple clang

Owner scope: `dependencies/WildMagic3p8/**` and
`dependencies/patches/wildmagic3p8_faf_macos.patch`.

## Result

All 386 `Wm3Foundation` translation units in `build/macos/compile_commands.json` now compile
with `macos/tu.sh` (before this pass, 32 failed with 81 distinct errors). No files are excluded
and there are no stubs. Nothing under `src/` or `macos/` was touched.

## What changed (pure C++ conformance, no numeric change)

Wild Magic 3.8 was written for MSVC 7.1/8.0, which looks up unqualified names in dependent
base classes and accepts explicit specializations after the class is instantiated. Clang
does neither outside `-fms-compatibility`. The fixes:

1. **Unqualified use of members of a dependent base: 66 call sites in 23 files.** Each one
   gets `this->` (`GetPosition`, `GetSecondDerivative`, `GetSpeed`, `GetKeyInfo`,
   `ComputeDerivative`, `SetPolynomial`, `m_oFunction`, `Det2`/`Det3`/`Det4`). `this->` gives the
   same lookup MSVC does, and clang's own error recovery (and its `-fms-compatibility` path)
   does the same thing. Virtual dispatch is kept: `Base::` was not used, because it would
   turn virtual calls such as `GetPosition` into static calls.
   - Curves: `Wm3CubicPolynomialCurve{2,3}.cpp`, `Wm3MultipleCurve{2,3}.cpp`,
     `Wm3NaturalSpline{2,3}.cpp`, `Wm3SingleCurve{2,3}.cpp`, `Wm3TCBSpline{2,3}.cpp`
   - Interpolation: `Wm3IntpAkima{Nonuniform,Uniform}1.cpp`,
     `Wm3IntpBSplineUniform{1,2,3,4,N}.cpp`
   - Numerics: `Wm3OdeEuler.cpp`, `Wm3OdeImplicitEuler.cpp`, `Wm3OdeMidpoint.cpp`,
     `Wm3OdeRungeKutta4.cpp`
   - RationalArithmetic: `Wm3Query2Filtered.inl`, `Wm3Query3Filtered.inl`. These were the only
     errors in `Wm3ConvexHull{2,3}.cpp`, `Wm3Delaunay{2,3}.cpp` and `Wm3TriangulateEC.cpp`.
2. **Explicit specialization of static data members after instantiation.**
   - `System/Wm3Platforms.h`: in the `__APPLE__` branch, `WM3_INSTANTIATE_BEFORE` is now also
     defined for `__clang__` (it was only defined for CodeWarrior). The four files that already
     have a `WM3_INSTANTIATE_BEFORE` branch (`Wm3IntpBilinear2.cpp`, `Wm3IntpTrilinear3.cpp`,
     `Wm3IntpBicubic2.cpp`, `Wm3IntpTricubic3.cpp`) use it with no further edits. I checked
     that both branches in each file hold the same table literals in a different order.
   - `Miscellaneous/Wm3GridGraph2.cpp` had no such branch. I added the same
     `#ifdef WM3_INSTANTIATE_BEFORE ... #else <original> #endif` structure, so the
     `ms_aaiIndex` specializations come before `template class GridGraph2<T>;`. The
     original order stays in the `#else` branch.
   - The macro is used only in these five files (nothing in `src/` uses it).
3. **`Physics/Wm3LCPPolyDist.cpp`:** `LCPSolver::LCPSolver(...)` names the constructor, not
   the type (MSVC reads it as a temporary). It is now `LCPSolver(...)`, which builds the same
   temporary, solves in the constructor and is destroyed at the end of the statement, as
   before.

All edits keep CRLF line endings. They were made byte-wise and the patch carries the CRs.

## The patch

`dependencies/patches/wildmagic3p8_faf_macos.patch` is regenerated in `git diff` format,
the same as before: LF headers and CRLF body lines. It covers 26 files: the existing
`Wm3Platforms.h` endianness hunk, merged with the new `WM3_INSTANTIATE_BEFORE` hunk in the
same file, plus the 25 sources above. Here is how it was produced:

1. `git archive HEAD` of the submodule went into a temp dir, followed by
   `git -c core.autocrlf=false apply --unsafe-paths --whitespace=nowarn
   wildmagic3p8_faf_required.patch`, then a commit to a throwaway repo.
2. The submodule's modified and untracked files were copied over that tree. The
   faf_required files came out byte-identical, so only the macOS changes show in the diff.
3. `git diff --src-prefix=a/ --dst-prefix=b/` produced the patch.

Here is how it was verified:

- A fresh `git archive HEAD` with `faf_required` then `faf_macos` applied is identical to the
  submodule work tree (`diff -r`, excluding `.git`; it reported no differences).
- In the submodule, `git apply --check --reverse` passes for both patches.

## Open issues / notes

- I could not show object-code equality against the original sources with
  `-fms-compatibility`, because the macOS SDK headers do not compile in that mode (`va_list`
  redefinition). The equivalence argument is the C++ one above: `this->` is the lookup MSVC
  performs, the table data is unchanged, and the temporary has the same lifetime.
- The engine (`src/`) only includes Wm3 headers whose templates Wm3Foundation explicitly
  instantiates (Vector2/3, Quaternion, Box2/3, AABB3, Sphere3, Segment3, Plane3, Line3,
  Circle2, the Intr*/Dist* classes it uses). Those are covered by this compile pass. The only
  header-only templates with dependent bases are in ImageAnalysis, RationalArithmetic,
  TCachedArray, Arc2, LinComp and ConvexPolyhedron3, and `src/` does not use them.

## Lead requests

- The macOS build must make sure the submodule carries `wildmagic3p8_faf_macos.patch` on top
  of `wildmagic3p8_faf_required.patch`. Right now it is applied by hand in the work tree.
  `cmake/FafDependencies.cmake` lists only `wildmagic3p8_faf_required.patch` for
  WildMagic3p8. The macOS configure could apply/verify the macOS patch the same way, for
  example with `git apply --check --reverse` as a guard.
