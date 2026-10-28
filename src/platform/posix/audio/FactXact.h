#pragma once

// POSIX build: the engine's XACT 2 runtime on FAudio's FACT (contract with
// moho/audio/AudioEngine.cpp). Implementation and notes: FactXact.cpp,
// macos/porting/audio.md.
//
// On Windows `func_CreateXACTinstance` creates xactengine2_9.dll's COM object, whose
// vtables the engine calls through moho::IXACTEngine / IXACTSoundBank / IXACTCue /
// IXACTWaveBank (AudioEngine.h). On POSIX it calls CreateXactEngine instead, which returns
// an object with those same vtables whose methods forward to FACT's C API: FACT reads FA's
// XACT banks (content version 43) and plays through FAudio's SDL3 output.

#include <cstdint>

namespace faf_posix::audio
{
  /**
   * What `CoCreateInstance(CLSID_XACTEngine, ..., IID_IXACTEngine, outEngine)` does for
   * `func_CreateXACTinstance`: stores a new, uninitialized `moho::IXACTEngine*` (reference
   * count 1) in `*outEngine` and returns S_OK, or stores null and returns an HRESULT error.
   * `mode` is func_CreateXACTinstance's (bit 0 audition, bit 1 debug engine); FACT has
   * neither, so both get the retail engine (logged once).
   */
  [[nodiscard]] int CreateXactEngine(std::uint32_t mode, void** outEngine) noexcept;
} // namespace faf_posix::audio
