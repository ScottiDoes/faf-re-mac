// POSIX build: the engine's XACT 2 interfaces on FAudio's FACT. Contract: FactXact.h.
//
// The engine (moho/audio/AudioEngine.cpp, CUserSoundManager.cpp, HSound.cpp) calls XACT
// only through the four recovered xactengine2_9 vtables in AudioEngine.h. Each adapter
// below implements one of them on the matching FACT object:
//
//   moho::IXACTEngine    -> FACTAudioEngine   (FactEngine)
//   moho::IXACTSoundBank -> FACTSoundBank     (FactSoundBank)
//   moho::IXACTWaveBank  -> FACTWaveBank      (FactWaveBank)
//   moho::IXACTCue       -> FACTCue           (FactCue)
//
// Return values are XACT's: HRESULTs, with FACT's codes (FACTENGINE_E_* are XACT's
// numbers) passed through and FACT's bare "1" failures reported as E_FAIL.
//
// What differs from calling FACT directly, and why:
// - Runtime parameters: the engine passes XACT 2.5's AudioRuntimeParams (no XAudio2
//   fields). They are copied field by field into a FACTRuntimeParameters; FACT then
//   creates its own FAudio instance and mastering voice on the default SDL output.
// - Streaming wave banks: the engine opens each streaming .xwb with CreateFileA and hands
//   XACT that HANDLE. FACT reads through the engine-level file callbacks, which here call
//   the Files area's ReadFile with an OVERLAPPED offset (a positioned read) on that HANDLE
//   (or the engine's own callbacks, when it supplies them).
// - Notifications: FACT's FACTNotification is converted field by field into XACT's
//   (same packed layout) with FACT object pointers mapped back to the adapters the engine
//   knows, then given to the engine's callback (func_HandleSoundEvent).
// - Cue lifetime: XACT frees a sound bank's cues with the bank (IXACTSoundBank::Destroy,
//   IXACTEngine::ShutDown). Adapters of cues whose FACT object is gone that way are kept
//   until the engine calls IXACTCue::Destroy and report XACT_CUESTATE_STOPPED meanwhile,
//   so a late Stop/GetState/Destroy from the engine does no harm.
// - Threads: FACT serialises its API with its own lock and runs its cue updates on its
//   own thread ("FACT Thread", every 10 ms) and the mix on SDL's audio thread. The engine
//   calls these adapters from the main thread (CUserSoundManager, SND_Frame's DoWork).
//   The adapter registry has its own mutex, never held while calling into FACT, because
//   FACT calls the notification callback with its lock held.
//
// Nothing here reaches the simulation: sound is user-side only (macos/porting/audio.md).

#include "platform/posix/audio/FactXact.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <FACT.h>
#include <FAudio.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>

#include "gpg/core/utils/Logging.h"
#include "moho/audio/AudioEngine.h"

namespace
{
  using moho::AudioNotification;
  using moho::AudioNotificationDesc;
  using moho::AudioRuntimeParams;

  class FactEngine;
  class FactSoundBank;
  class FactWaveBank;
  class FactCue;

  // XACT 2's engine interface id and IUnknown (func_CreateXACTinstance's riid).
  constexpr GUID kIidXactEngine = {0x893ff2e4u, 0x8d03u, 0x4d5fu, {0xb0u, 0xaau, 0x36u, 0x3au, 0x9cu, 0xbbu, 0xf4u, 0x37u}};
  constexpr GUID kIidUnknown = {0x00000000u, 0x0000u, 0x0000u, {0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u}};

  static_assert(sizeof(FACTOverlapped) == sizeof(OVERLAPPED), "FACT's overlapped block is Win32's OVERLAPPED");
  static_assert(sizeof(FAudioWaveFormatExtensible) == sizeof(WAVEFORMATEXTENSIBLE), "final mix format size");
  static_assert(sizeof(FACTNotification) == sizeof(AudioNotification), "FACT and XACT notifications have one layout");

  /// FACT returns 0, an XACT/FAudio HRESULT (negative), or 1 for an unspecified failure.
  [[nodiscard]] int ToHresult(const std::uint32_t result) noexcept
  {
    if (result == 0u) {
      return S_OK;
    }
    const auto code = static_cast<std::int32_t>(result);
    return code < 0 ? code : E_FAIL;
  }

  // --- Registry: FACT objects -> adapters, and the engine's callbacks ----------------
  struct Registry
  {
    std::mutex mutex;
    std::unordered_map<const FACTSoundBank*, FactSoundBank*> soundBanks;
    std::unordered_map<const FACTWaveBank*, FactWaveBank*> waveBanks;
    std::unordered_map<const FACTCue*, FactCue*> cues;
  };

  /// Never destroyed: FACT threads may still deliver a notification during exit.
  [[nodiscard]] Registry& GetRegistry() noexcept
  {
    static Registry* const registry = new Registry();
    return *registry;
  }

  // The engine's runtime-parameter callbacks. Every AudioEngine is initialized from the
  // one SoundConfiguration (func_InitSound), so they are process-wide here too.
  std::atomic<XACT_NOTIFICATION_CALLBACK> sClientNotify{nullptr};
  std::atomic<XACT_READFILE_CALLBACK> sClientReadFile{nullptr};
  std::atomic<XACT_GETOVERLAPPEDRESULT_CALLBACK> sClientGetOverlappedResult{nullptr};

  // --- Streaming wave-bank I/O: hFile is the engine's CreateFileA HANDLE ---------------
  int32_t FACTCALL ReadStreamingFile(
    void* const file, void* const buffer, const uint32_t bytes, uint32_t* const bytesRead, FACTOverlapped* const factOverlapped
  )
  {
    OVERLAPPED overlapped{};
    overlapped.Offset = factOverlapped->Offset;
    overlapped.OffsetHigh = factOverlapped->OffsetHigh;
    DWORD read = 0;
    BOOL ok = FALSE;
    if (const XACT_READFILE_CALLBACK client = sClientReadFile.load(std::memory_order_acquire); client != nullptr) {
      ok = client(static_cast<HANDLE>(file), buffer, bytes, &read, &overlapped);
    } else {
      // The Files area completes an OVERLAPPED read before returning (a pread at
      // Offset), setting Internal/InternalHigh as Windows does on completion.
      ok = ::ReadFile(static_cast<HANDLE>(file), buffer, bytes, &read, &overlapped);
      read = static_cast<DWORD>(overlapped.InternalHigh);
    }
    factOverlapped->Internal = reinterpret_cast<void*>(overlapped.Internal);
    factOverlapped->InternalHigh = reinterpret_cast<void*>(overlapped.InternalHigh);
    if (bytesRead != nullptr) {
      *bytesRead = read;
    }
    if (ok == FALSE && overlapped.Internal == 0u) {
      // A failed read must not look like a completed one to GetOverlappedResult.
      factOverlapped->Internal = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xC0000185u)); // STATUS_IO_DEVICE_ERROR
      factOverlapped->InternalHigh = nullptr;
    }
    return ok != FALSE ? 1 : 0;
  }

  int32_t FACTCALL GetStreamingOverlappedResult(
    void* const file, FACTOverlapped* const factOverlapped, uint32_t* const transferred, const int32_t wait
  )
  {
    if (const XACT_GETOVERLAPPEDRESULT_CALLBACK client = sClientGetOverlappedResult.load(std::memory_order_acquire);
        client != nullptr) {
      OVERLAPPED overlapped{};
      overlapped.Internal = reinterpret_cast<ULONG_PTR>(factOverlapped->Internal);
      overlapped.InternalHigh = reinterpret_cast<ULONG_PTR>(factOverlapped->InternalHigh);
      overlapped.Offset = factOverlapped->Offset;
      overlapped.OffsetHigh = factOverlapped->OffsetHigh;
      DWORD done = 0;
      const BOOL ok = client(static_cast<HANDLE>(file), &overlapped, &done, wait);
      if (transferred != nullptr) {
        *transferred = done;
      }
      return ok != FALSE ? 1 : 0;
    }
    // Reads complete synchronously (ReadStreamingFile), so the result is already there.
    if (transferred != nullptr) {
      *transferred = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(factOverlapped->InternalHigh));
    }
    // FACT loops on this until it returns nonzero: a failed read reports what it read (0)
    // instead of spinning forever; FACT then plays silence for that packet.
    return 1;
  }

  void FACTCALL OnFactNotification(const FACTNotification* note);

  // --- Cues --------------------------------------------------------------------------
  class FactCue final : public moho::IXACTCue
  {
  public:
    FactCue(FactSoundBank* const bank, FACTCue* const cue) noexcept
      : mBank(bank)
      , mCue(cue)
    {
    }

    std::int32_t __stdcall Play() override
    {
      return mCue != nullptr ? ToHresult(FACTCue_Play(mCue)) : static_cast<int>(XACTENGINE_E_INVALIDUSAGE);
    }

    void __stdcall Stop(const std::int32_t stopFlags) override
    {
      if (mCue != nullptr) {
        (void)FACTCue_Stop(mCue, static_cast<uint32_t>(stopFlags));
      }
    }

    std::int32_t __stdcall GetState(std::int32_t* const outState) override
    {
      if (outState == nullptr) {
        return E_POINTER;
      }
      if (mCue == nullptr) {
        *outState = XACT_CUESTATE_STOPPED; // freed with its sound bank
        return S_OK;
      }
      uint32_t state = 0;
      const int result = ToHresult(FACTCue_GetState(mCue, &state));
      *outState = static_cast<std::int32_t>(state);
      return result;
    }

    std::int32_t __stdcall Destroy() override;

    void __stdcall Reserved10() override { FAF_POSIX_STUB("SND: IXACTCue slot 4 (+0x10) is not called by the engine"); }
    void __stdcall Reserved14() override { FAF_POSIX_STUB("SND: IXACTCue slot 5 (+0x14) is not called by the engine"); }
    void __stdcall Reserved18() override { FAF_POSIX_STUB("SND: IXACTCue slot 6 (+0x18) is not called by the engine"); }
    void __stdcall Reserved1C() override { FAF_POSIX_STUB("SND: IXACTCue slot 7 (+0x1C) is not called by the engine"); }

    std::int32_t __stdcall SetMatrixCoefficients(
      const std::uint32_t srcChannelCount, const std::uint32_t dstChannelCount, const float* const matrixCoefficients
    ) override
    {
      if (mCue == nullptr) {
        return static_cast<int>(XACTENGINE_E_INVALIDUSAGE);
      }
      if (matrixCoefficients == nullptr || srcChannelCount == 0u || srcChannelCount > 2u || dstChannelCount == 0u ||
          dstChannelCount > 8u) {
        return E_INVALIDARG; // FACT keeps at most 2 x 8 coefficients per cue
      }
      return ToHresult(
        FACTCue_SetMatrixCoefficients(mCue, srcChannelCount, dstChannelCount, const_cast<float*>(matrixCoefficients))
      );
    }

    std::uint16_t __stdcall GetVariableIndex(const char* const variableName) override
    {
      if (mCue == nullptr || variableName == nullptr) {
        return XACTVARIABLEINDEX_INVALID;
      }
      return FACTCue_GetVariableIndex(mCue, variableName);
    }

    std::int32_t __stdcall SetVariable(const std::uint16_t variableIndex, const float value) override
    {
      if (mCue == nullptr) {
        return static_cast<int>(XACTENGINE_E_INVALIDUSAGE);
      }
      if (variableIndex == XACTVARIABLEINDEX_INVALID) {
        return static_cast<int>(XACTENGINE_E_INVALIDVARIABLEINDEX); // FACT returns 1
      }
      return ToHresult(FACTCue_SetVariable(mCue, variableIndex, value));
    }

    [[nodiscard]] FACTCue* Handle() const noexcept { return mCue; }

    FactSoundBank* mBank; // guarded by the registry mutex once the cue is registered
    FACTCue* mCue;        // null once FACT freed it with the sound bank or engine
  };

  // --- Sound banks -------------------------------------------------------------------
  class FactSoundBank final : public moho::IXACTSoundBank
  {
  public:
    FactSoundBank(FactEngine* const engine, FACTSoundBank* const bank) noexcept
      : mEngine(engine)
      , mBank(bank)
    {
    }

    std::uint16_t __stdcall GetCueIndex(const char* const cueName) override
    {
      if (mBank == nullptr || cueName == nullptr) {
        return XACTINDEX_INVALID;
      }
      return FACTSoundBank_GetCueIndex(mBank, cueName);
    }

    std::int32_t __stdcall Reserved04() override
    {
      FAF_POSIX_STUB("SND: IXACTSoundBank slot 1 (+0x04) is not called by the engine");
      return E_NOTIMPL;
    }

    std::int32_t __stdcall Reserved08() override
    {
      FAF_POSIX_STUB("SND: IXACTSoundBank slot 2 (+0x08) is not called by the engine");
      return E_NOTIMPL;
    }

    std::int32_t __stdcall Prepare(
      const std::uint16_t cueIndex, const std::uint32_t flags, const std::uint32_t timeOffset, moho::IXACTCue** const outCue
    ) override
    {
      return Start(cueIndex, flags, timeOffset, outCue, false);
    }

    std::int32_t __stdcall Play(
      const std::uint16_t cueIndex, const std::uint32_t flags, const std::uint32_t timeOffset, moho::IXACTCue** const outCue
    ) override
    {
      return Start(cueIndex, flags, timeOffset, outCue, true);
    }

    std::int32_t __stdcall Reserved14() override
    {
      FAF_POSIX_STUB("SND: IXACTSoundBank slot 5 (+0x14) is not called by the engine");
      return E_NOTIMPL;
    }

    void __stdcall Destroy() override;

    /// Registry mutex held: FACT freed (or is about to free) this bank's cues.
    void ForgetCuesLocked(Registry& registry) noexcept
    {
      for (FactCue* const cue : mCues) {
        if (cue->mCue != nullptr) {
          registry.cues.erase(cue->mCue);
        }
        cue->mCue = nullptr;
        cue->mBank = nullptr;
      }
      mCues.clear();
    }

    [[nodiscard]] FACTSoundBank* Handle() const noexcept { return mBank; }

    FactEngine* mEngine;
    FACTSoundBank* mBank;                 // null once the engine shut down
    std::unordered_set<FactCue*> mCues;   // guarded by the registry mutex

  private:
    int Start(std::uint16_t cueIndex, std::uint32_t flags, std::uint32_t timeOffset, moho::IXACTCue** outCue, bool play);
  };

  int FactSoundBank::Start(
    const std::uint16_t cueIndex,
    const std::uint32_t flags,
    const std::uint32_t timeOffset,
    moho::IXACTCue** const outCue,
    const bool play
  )
  {
    if (outCue != nullptr) {
      *outCue = nullptr;
    }
    if (mBank == nullptr) {
      return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
    }
    // XACTTIME is a signed millisecond offset.
    const auto offset = static_cast<int32_t>(timeOffset);
    if (outCue == nullptr) {
      if (!play) {
        return E_POINTER; // XACT: Prepare needs somewhere to put the cue
      }
      // Fire and forget: FACT owns and frees the cue when it stops.
      return ToHresult(FACTSoundBank_Play(mBank, cueIndex, flags, offset, nullptr));
    }

    FACTCue* cue = nullptr;
    const uint32_t raw = play ? FACTSoundBank_Play(mBank, cueIndex, flags, offset, &cue)
                              : FACTSoundBank_Prepare(mBank, cueIndex, flags, offset, &cue);
    const int result = ToHresult(raw);
    if (cue == nullptr) {
      return result >= 0 ? E_FAIL : result;
    }
    if (result < 0 && result != static_cast<int>(XACTENGINE_E_INSTANCELIMITFAILTOPLAY)) {
      // The engine drops the cue on any other error; do not leak FACT's.
      (void)FACTCue_Destroy(cue);
      return result;
    }

    auto* const adapter = new (std::nothrow) FactCue(this, cue);
    if (adapter == nullptr) {
      (void)FACTCue_Destroy(cue);
      return E_OUTOFMEMORY;
    }
    {
      Registry& registry = GetRegistry();
      const std::lock_guard<std::mutex> lock(registry.mutex);
      registry.cues[cue] = adapter;
      mCues.insert(adapter);
    }
    *outCue = adapter;
    return result;
  }

  std::int32_t __stdcall FactCue::Destroy()
  {
    FACTCue* cue = nullptr;
    {
      Registry& registry = GetRegistry();
      const std::lock_guard<std::mutex> lock(registry.mutex);
      cue = mCue;
      if (cue != nullptr) {
        registry.cues.erase(cue);
      }
      if (mBank != nullptr) {
        mBank->mCues.erase(this);
      }
      mCue = nullptr;
      mBank = nullptr;
    }
    const int result = cue != nullptr ? ToHresult(FACTCue_Destroy(cue)) : S_OK;
    delete this;
    return result;
  }

  void __stdcall FactSoundBank::Destroy()
  {
    FACTSoundBank* bank = nullptr;
    {
      Registry& registry = GetRegistry();
      const std::lock_guard<std::mutex> lock(registry.mutex);
      bank = mBank;
      ForgetCuesLocked(registry); // FACTSoundBank_Destroy frees them, as XACT does
      if (bank != nullptr) {
        registry.soundBanks.erase(bank);
      }
      mBank = nullptr;
    }
    if (bank != nullptr) {
      (void)FACTSoundBank_Destroy(bank);
    }
    delete this;
  }

  // --- Wave banks --------------------------------------------------------------------
  class FactWaveBank final : public moho::IXACTWaveBank
  {
  public:
    FactWaveBank(FactEngine* const engine, FACTWaveBank* const bank) noexcept
      : mEngine(engine)
      , mBank(bank)
    {
    }

    void __stdcall Destroy() override
    {
      FACTWaveBank* bank = nullptr;
      {
        Registry& registry = GetRegistry();
        const std::lock_guard<std::mutex> lock(registry.mutex);
        bank = mBank;
        if (bank != nullptr) {
          registry.waveBanks.erase(bank);
        }
        mBank = nullptr;
      }
      if (bank != nullptr) {
        (void)FACTWaveBank_Destroy(bank);
      }
      delete this;
    }

    [[nodiscard]] FACTWaveBank* Handle() const noexcept { return mBank; }

    FactEngine* mEngine;
    FACTWaveBank* mBank; // null once the engine shut down
  };

  // --- Engine ------------------------------------------------------------------------
  class FactEngine final : public moho::IXACTEngine
  {
  public:
    explicit FactEngine(FACTAudioEngine* const engine) noexcept
      : mEngine(engine)
    {
    }

    std::int32_t __stdcall QueryInterface(const void* const riid, void** const outObject) override
    {
      if (outObject == nullptr) {
        return E_POINTER;
      }
      *outObject = nullptr;
      if (riid == nullptr) {
        return E_POINTER;
      }
      if (std::memcmp(riid, &kIidXactEngine, sizeof(GUID)) != 0 && std::memcmp(riid, &kIidUnknown, sizeof(GUID)) != 0) {
        return E_NOINTERFACE;
      }
      AddRef();
      *outObject = static_cast<moho::IXACTEngine*>(this);
      return S_OK;
    }

    std::uint32_t __stdcall AddRef() override { return mRefCount.fetch_add(1u) + 1u; }

    std::uint32_t __stdcall Release() override
    {
      const std::uint32_t remaining = mRefCount.fetch_sub(1u) - 1u;
      if (remaining == 0u) {
        ShutDownImpl();
        (void)FACTAudioEngine_Release(mEngine);
        delete this;
      }
      return remaining;
    }

    std::int32_t __stdcall GetRendererCount(std::uint16_t* const outRendererCount) override
    {
      if (outRendererCount == nullptr) {
        return E_POINTER;
      }
      return ToHresult(FACTAudioEngine_GetRendererCount(mEngine, outRendererCount));
    }

    std::int32_t __stdcall GetRendererDetails(const std::uint16_t rendererIndex, void* const outRendererDetails) override
    {
      if (outRendererDetails == nullptr) {
        return E_POINTER;
      }
      FACTRendererDetails details{};
      const int result = ToHresult(FACTAudioEngine_GetRendererDetails(mEngine, rendererIndex, &details));
      if (result < 0) {
        return result;
      }
      // FACT's text is UTF-16; XACT_RENDERER_DETAILS holds WCHAR (32-bit here, in memory only).
      auto* const out = static_cast<XACT_RENDERER_DETAILS*>(outRendererDetails);
      *out = XACT_RENDERER_DETAILS{};
      for (int i = 0; i < 0xFF - 1 && details.rendererID[i] != 0; ++i) {
        out->rendererID[i] = static_cast<WCHAR>(static_cast<std::uint16_t>(details.rendererID[i]));
      }
      for (int i = 0; i < 0xFF - 1 && details.displayName[i] != 0; ++i) {
        out->displayName[i] = static_cast<WCHAR>(static_cast<std::uint16_t>(details.displayName[i]));
      }
      out->defaultDevice = details.defaultDevice != 0 ? TRUE : FALSE;
      return result;
    }

    std::int32_t __stdcall GetFinalMixFormat(void* const outWaveFormatExtensible) override
    {
      if (outWaveFormatExtensible == nullptr) {
        return E_POINTER;
      }
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      FAudioWaveFormatExtensible format{};
      const int result = ToHresult(FACTAudioEngine_GetFinalMixFormat(mEngine, &format));
      auto* const out = static_cast<WAVEFORMATEXTENSIBLE*>(outWaveFormatExtensible);
      out->Format.wFormatTag = format.Format.wFormatTag;
      out->Format.nChannels = format.Format.nChannels;
      out->Format.nSamplesPerSec = format.Format.nSamplesPerSec;
      out->Format.nAvgBytesPerSec = format.Format.nAvgBytesPerSec;
      out->Format.nBlockAlign = format.Format.nBlockAlign;
      out->Format.wBitsPerSample = format.Format.wBitsPerSample;
      out->Format.cbSize = format.Format.cbSize;
      out->Samples.wValidBitsPerSample = format.Samples.wValidBitsPerSample;
      out->dwChannelMask = format.dwChannelMask;
      static_assert(sizeof(out->SubFormat) == sizeof(format.SubFormat), "GUID size");
      std::memcpy(&out->SubFormat, &format.SubFormat, sizeof(out->SubFormat));
      return result;
    }

    std::int32_t __stdcall Initialize(const AudioRuntimeParams* runtimeParams) override;

    void __stdcall ShutDown() override { ShutDownImpl(); }

    std::int32_t __stdcall DoWork() override
    {
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      return ToHresult(FACTAudioEngine_DoWork(mEngine));
    }

    std::int32_t __stdcall CreateSoundBank(
      const void* const data,
      const std::int32_t sizeBytes,
      const std::uint32_t flags,
      const std::uint32_t allocAttributes,
      moho::IXACTSoundBank** const outSoundBank
    ) override
    {
      if (outSoundBank == nullptr || data == nullptr || sizeBytes <= 0) {
        return outSoundBank == nullptr ? E_POINTER : E_INVALIDARG;
      }
      *outSoundBank = nullptr;
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      FACTSoundBank* bank = nullptr;
      const int result = ToHresult(
        FACTAudioEngine_CreateSoundBank(mEngine, data, static_cast<uint32_t>(sizeBytes), flags, allocAttributes, &bank)
      );
      if (result < 0 || bank == nullptr) {
        return result < 0 ? result : E_FAIL;
      }
      auto* const adapter = new (std::nothrow) FactSoundBank(this, bank);
      if (adapter == nullptr) {
        (void)FACTSoundBank_Destroy(bank);
        return E_OUTOFMEMORY;
      }
      {
        Registry& registry = GetRegistry();
        const std::lock_guard<std::mutex> lock(registry.mutex);
        registry.soundBanks[bank] = adapter;
      }
      *outSoundBank = adapter;
      return result;
    }

    std::int32_t __stdcall CreateInMemoryWaveBank(
      const void* const data,
      const std::int32_t sizeBytes,
      const std::uint32_t flags,
      const std::uint32_t allocAttributes,
      moho::IXACTWaveBank** const outWaveBank
    ) override
    {
      if (outWaveBank == nullptr || data == nullptr || sizeBytes <= 0) {
        return outWaveBank == nullptr ? E_POINTER : E_INVALIDARG;
      }
      *outWaveBank = nullptr;
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      FACTWaveBank* bank = nullptr;
      const int result = ToHresult(FACTAudioEngine_CreateInMemoryWaveBank(
        mEngine, data, static_cast<uint32_t>(sizeBytes), flags, allocAttributes, &bank
      ));
      return WrapWaveBank(result, bank, outWaveBank);
    }

    std::int32_t __stdcall CreateStreamingWaveBank(const void* const streamingParams, moho::IXACTWaveBank** const outWaveBank) override
    {
      if (outWaveBank == nullptr || streamingParams == nullptr) {
        return E_POINTER;
      }
      *outWaveBank = nullptr;
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      const auto* const params = static_cast<const XACT_STREAMING_PARAMETERS*>(streamingParams);
      FACTStreamingParameters factParams{};
      factParams.file = params->file;
      factParams.offset = params->offset;
      factParams.flags = params->flags;
      factParams.packetSize = params->packetSize;
      FACTWaveBank* bank = nullptr;
      const int result = ToHresult(FACTAudioEngine_CreateStreamingWaveBank(mEngine, &factParams, &bank));
      return WrapWaveBank(result, bank, outWaveBank);
    }

    std::int32_t __stdcall PrepareWave(
      std::uint32_t, const char*, std::uint16_t, std::uint32_t, std::uint32_t, std::uint8_t, void** const outWave
    ) override
    {
      // The engine plays cues only; it has no IXACTWave interface to hand a wave back on.
      FAF_POSIX_STUB("SND: IXACTEngine::PrepareWave is not called by the engine");
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return E_NOTIMPL;
    }

    std::int32_t __stdcall PrepareInMemoryWave(
      std::uint32_t, const void*, std::uint32_t*, std::uint8_t*, std::uint32_t, std::uint8_t, void** const outWave
    ) override
    {
      FAF_POSIX_STUB("SND: IXACTEngine::PrepareInMemoryWave is not called by the engine");
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return E_NOTIMPL;
    }

    std::int32_t __stdcall PrepareStreamingWave(
      std::uint32_t, const void*, const void*, std::uint32_t, std::uint32_t*, std::uint32_t, std::uint8_t, void** const outWave
    ) override
    {
      FAF_POSIX_STUB("SND: IXACTEngine::PrepareStreamingWave is not called by the engine");
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return E_NOTIMPL;
    }

    std::int32_t __stdcall RegisterNotification(const AudioNotificationDesc* const desc) override
    {
      return Notification(desc, true);
    }

    std::int32_t __stdcall UnRegisterNotification(const AudioNotificationDesc* const desc) override
    {
      return Notification(desc, false);
    }

    std::uint16_t __stdcall GetCategory(const char* const categoryName) override
    {
      if (!mInitialized || categoryName == nullptr) {
        return XACTCATEGORY_INVALID;
      }
      return FACTAudioEngine_GetCategory(mEngine, categoryName);
    }

    void __stdcall Stop(const std::uint16_t category, const std::int32_t flags) override
    {
      if (mInitialized) {
        (void)FACTAudioEngine_Stop(mEngine, category, static_cast<uint32_t>(flags));
      }
    }

    std::int32_t __stdcall SetVolume(const std::uint16_t category, const float volume) override
    {
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      if (category == XACTCATEGORY_INVALID) {
        return static_cast<int>(XACTENGINE_E_INVALIDCATEGORY);
      }
      return ToHresult(FACTAudioEngine_SetVolume(mEngine, category, volume));
    }

    std::int32_t __stdcall Pause(const std::uint16_t category, const std::int32_t pause) override
    {
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      if (category == XACTCATEGORY_INVALID) {
        return static_cast<int>(XACTENGINE_E_INVALIDCATEGORY);
      }
      return ToHresult(FACTAudioEngine_Pause(mEngine, category, pause));
    }

    std::uint16_t __stdcall GetGlobalVariableIndex(const char* const variableName) override
    {
      if (!mInitialized || variableName == nullptr) {
        return XACTVARIABLEINDEX_INVALID;
      }
      return FACTAudioEngine_GetGlobalVariableIndex(mEngine, variableName);
    }

    std::int32_t __stdcall SetGlobalVariable(const std::uint16_t variableIndex, const float value) override
    {
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      if (variableIndex == XACTVARIABLEINDEX_INVALID) {
        return static_cast<int>(XACTENGINE_E_INVALIDVARIABLEINDEX);
      }
      return ToHresult(FACTAudioEngine_SetGlobalVariable(mEngine, variableIndex, value));
    }

    std::int32_t __stdcall GetGlobalVariable(const std::uint16_t variableIndex, float* const outValue) override
    {
      if (outValue == nullptr) {
        return E_POINTER;
      }
      if (!mInitialized) {
        return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
      }
      if (variableIndex == XACTVARIABLEINDEX_INVALID) {
        return static_cast<int>(XACTENGINE_E_INVALIDVARIABLEINDEX);
      }
      return ToHresult(FACTAudioEngine_GetGlobalVariable(mEngine, variableIndex, outValue));
    }

  private:
    int WrapWaveBank(const int result, FACTWaveBank* const bank, moho::IXACTWaveBank** const outWaveBank)
    {
      if (result < 0 || bank == nullptr) {
        return result < 0 ? result : E_FAIL;
      }
      auto* const adapter = new (std::nothrow) FactWaveBank(this, bank);
      if (adapter == nullptr) {
        (void)FACTWaveBank_Destroy(bank);
        return E_OUTOFMEMORY;
      }
      {
        Registry& registry = GetRegistry();
        const std::lock_guard<std::mutex> lock(registry.mutex);
        registry.waveBanks[bank] = adapter;
      }
      *outWaveBank = adapter;
      return result;
    }

    int Notification(const AudioNotificationDesc* desc, bool registering);

    /// IXACTEngine::ShutDown: FACT frees every bank and cue; their adapters stay valid
    /// (dead) until the engine destroys them.
    void ShutDownImpl()
    {
      if (!mInitialized) {
        return;
      }
      {
        Registry& registry = GetRegistry();
        const std::lock_guard<std::mutex> lock(registry.mutex);
        for (auto it = registry.soundBanks.begin(); it != registry.soundBanks.end();) {
          if (it->second->mEngine == this) {
            it->second->ForgetCuesLocked(registry);
            it->second->mBank = nullptr;
            it = registry.soundBanks.erase(it);
          } else {
            ++it;
          }
        }
        for (auto it = registry.waveBanks.begin(); it != registry.waveBanks.end();) {
          if (it->second->mEngine == this) {
            it->second->mBank = nullptr;
            it = registry.waveBanks.erase(it);
          } else {
            ++it;
          }
        }
      }
      (void)FACTAudioEngine_ShutDown(mEngine);
      mInitialized = false;
      gpg::Logf("SND: FACT engine shut down");
    }

    FACTAudioEngine* mEngine;
    std::atomic<std::uint32_t> mRefCount{1u};
    bool mInitialized = false;
    bool mHasClientCallback = false;
    std::vector<int16_t> mRendererId; // UTF-16 copy of the runtime parameters' renderer id
  };

  std::int32_t __stdcall FactEngine::Initialize(const AudioRuntimeParams* const runtimeParams)
  {
    if (runtimeParams == nullptr) {
      return E_POINTER;
    }
    if (mInitialized) {
      return static_cast<int>(XACTENGINE_E_ALREADYINITIALIZED);
    }

    // XACT 2.5's parameter block, field by field (FACT's has two more fields at the end
    // and natural alignment; FAudio's header warns against copying it as bytes).
    FACTRuntimeParameters params{};
    params.lookAheadTime = runtimeParams->lookAheadTime;
    params.pGlobalSettingsBuffer = runtimeParams->pGlobalSettingsBuffer;
    params.globalSettingsBufferSize = runtimeParams->globalSettingsBufferSize;
    params.globalSettingsFlags = runtimeParams->globalSettingsFlags;
    params.globalSettingsAllocAttributes = runtimeParams->globalSettingsAllocAttributes;
    sClientReadFile.store(runtimeParams->fileIOCallbacks.readFileCallbackFunction, std::memory_order_release);
    sClientGetOverlappedResult.store(
      runtimeParams->fileIOCallbacks.getOverlappedResultCallbackFunction, std::memory_order_release
    );
    params.fileIOCallbacks.readFileCallback = &ReadStreamingFile;
    params.fileIOCallbacks.getOverlappedResultCallback = &GetStreamingOverlappedResult;
    mHasClientCallback = runtimeParams->fnNotificationCallback != nullptr;
    if (mHasClientCallback) {
      sClientNotify.store(runtimeParams->fnNotificationCallback, std::memory_order_release);
    }
    params.fnNotificationCallback = &OnFactNotification;
    if (runtimeParams->pRendererID != nullptr && runtimeParams->pRendererID[0] != 0) {
      for (const WCHAR* c = runtimeParams->pRendererID; *c != 0 && mRendererId.size() < 0xFE; ++c) {
        mRendererId.push_back(static_cast<int16_t>(*c));
      }
      mRendererId.push_back(0);
      params.pRendererID = mRendererId.data();
    }
    params.pXAudio2 = nullptr;        // FACT creates its FAudio instance ...
    params.pMasteringVoice = nullptr; // ... and mastering voice on the default SDL device

    const int result = ToHresult(FACTAudioEngine_Initialize(mEngine, &params));
    if (result < 0) {
      gpg::Warnf("SND: FACT could not start (0x%08x); is there an audio output device?", static_cast<unsigned>(result));
      return result;
    }
    mInitialized = true;

    FAudioWaveFormatExtensible format{};
    (void)FACTAudioEngine_GetFinalMixFormat(mEngine, &format);
    const unsigned version = FAudioLinkedVersion();
    const char* const driver = SDL_GetCurrentAudioDriver();
    gpg::Logf(
      "SND: FACT (FAudio %u.%02u.%u) started: %u channel(s) (mask 0x%x), %u Hz, SDL audio driver '%s'",
      version / 10000u,
      (version / 100u) % 100u,
      version % 100u,
      static_cast<unsigned>(format.Format.nChannels),
      static_cast<unsigned>(format.dwChannelMask),
      static_cast<unsigned>(format.Format.nSamplesPerSec),
      driver != nullptr ? driver : "?"
    );
    if (driver == nullptr) {
      // FAudio mixes even when SDL could not open an output (no device, a bad
      // SDL_AUDIO_DRIVER): everything works, nothing is heard.
      gpg::Warnf("SND: SDL has no audio output (%s); sound plays to nowhere", SDL_GetError());
    }
    return result;
  }

  int FactEngine::Notification(const AudioNotificationDesc* const desc, const bool registering)
  {
    if (desc == nullptr) {
      return E_POINTER;
    }
    if (!mInitialized) {
      return static_cast<int>(XACTENGINE_E_NOTINITIALIZED);
    }
    if (!mHasClientCallback) {
      return static_cast<int>(XACTENGINE_E_NONOTIFICATIONCALLBACK);
    }
    FACTNotificationDescription factDesc{};
    factDesc.type = desc->type;
    factDesc.flags = desc->flags;
    // The descriptor carries the engine's interface pointers (the adapters) under XACT 3's
    // opaque names (AudioEngine.cpp casts them); FACT wants its own objects.
    if (desc->pSoundBank != nullptr) {
      auto* const bank = static_cast<FactSoundBank*>(reinterpret_cast<moho::IXACTSoundBank*>(desc->pSoundBank));
      factDesc.pSoundBank = bank->Handle();
    }
    if (desc->pWaveBank != nullptr) {
      auto* const bank = static_cast<FactWaveBank*>(reinterpret_cast<moho::IXACTWaveBank*>(desc->pWaveBank));
      factDesc.pWaveBank = bank->Handle();
    }
    if (desc->pCue != nullptr) {
      auto* const cue = static_cast<FactCue*>(reinterpret_cast<moho::IXACTCue*>(desc->pCue));
      factDesc.pCue = cue->Handle();
    }
    factDesc.pWave = reinterpret_cast<FACTWave*>(desc->pWave); // the engine has no wave objects
    factDesc.cueIndex = desc->cueIndex;
    factDesc.waveIndex = desc->waveIndex;
    factDesc.pvContext = desc->pvContext;
    const uint32_t raw = registering ? FACTAudioEngine_RegisterNotification(mEngine, &factDesc)
                                     : FACTAudioEngine_UnRegisterNotification(mEngine, &factDesc);
    return ToHresult(raw);
  }

  // --- Notifications: FACT -> XACT -----------------------------------------------------
  template <typename Adapter, typename Map, typename Key>
  [[nodiscard]] Adapter* Lookup(const Map& map, const Key* const key) noexcept
  {
    if (key == nullptr) {
      return nullptr;
    }
    const auto it = map.find(key);
    return it != map.end() ? it->second : nullptr;
  }

  void FACTCALL OnFactNotification(const FACTNotification* const note)
  {
    const XACT_NOTIFICATION_CALLBACK client = sClientNotify.load(std::memory_order_acquire);
    if (note == nullptr || client == nullptr) {
      return;
    }

    AudioNotification out{};
    out.type = note->type;
    out.timeStamp = note->timeStamp;
    out.pvContext = note->pvContext;
    {
      Registry& registry = GetRegistry();
      const std::lock_guard<std::mutex> lock(registry.mutex);
      const auto soundBank = [&](const FACTSoundBank* bank) {
        auto* const adapter = Lookup<FactSoundBank>(registry.soundBanks, bank);
        return reinterpret_cast<IXACT3SoundBank*>(adapter != nullptr ? static_cast<moho::IXACTSoundBank*>(adapter) : nullptr);
      };
      const auto waveBank = [&](const FACTWaveBank* bank) {
        auto* const adapter = Lookup<FactWaveBank>(registry.waveBanks, bank);
        return reinterpret_cast<IXACT3WaveBank*>(adapter != nullptr ? static_cast<moho::IXACTWaveBank*>(adapter) : nullptr);
      };
      const auto cue = [&](const FACTCue* factCue) {
        auto* const adapter = Lookup<FactCue>(registry.cues, factCue);
        return reinterpret_cast<IXACT3Cue*>(adapter != nullptr ? static_cast<moho::IXACTCue*>(adapter) : nullptr);
      };

      switch (note->type) {
      case XACTNOTIFICATIONTYPE_CUEPREPARED:
      case XACTNOTIFICATIONTYPE_CUEPLAY:
      case XACTNOTIFICATIONTYPE_CUESTOP:
      case XACTNOTIFICATIONTYPE_CUEDESTROYED:
        out.cue.cueIndex = note->cue.cueIndex;
        out.cue.pSoundBank = soundBank(note->cue.pSoundBank);
        out.cue.pCue = cue(note->cue.pCue);
        break;
      case XACTNOTIFICATIONTYPE_MARKER:
        out.marker.cueIndex = note->marker.cueIndex;
        out.marker.pSoundBank = soundBank(note->marker.pSoundBank);
        out.marker.pCue = cue(note->marker.pCue);
        out.marker.marker = note->marker.marker;
        break;
      case XACTNOTIFICATIONTYPE_SOUNDBANKDESTROYED:
        out.soundBank.pSoundBank = soundBank(note->soundBank.pSoundBank);
        break;
      case XACTNOTIFICATIONTYPE_WAVEBANKDESTROYED:
      case XACTNOTIFICATIONTYPE_WAVEBANKPREPARED:
      case XACTNOTIFICATIONTYPE_WAVEBANKSTREAMING_INVALIDCONTENT:
        out.waveBank.pWaveBank = waveBank(note->waveBank.pWaveBank);
        break;
      case XACTNOTIFICATIONTYPE_LOCALVARIABLECHANGED:
      case XACTNOTIFICATIONTYPE_GLOBALVARIABLECHANGED:
        out.variable.cueIndex = note->variable.cueIndex;
        out.variable.pSoundBank = soundBank(note->variable.pSoundBank);
        out.variable.pCue = cue(note->variable.pCue);
        out.variable.variableIndex = note->variable.variableIndex;
        out.variable.variableValue = note->variable.variableValue;
        out.variable.local = note->variable.local;
        break;
      case XACTNOTIFICATIONTYPE_GUICONNECTED:
      case XACTNOTIFICATIONTYPE_GUIDISCONNECTED:
        out.gui.reserved = note->gui.reserved;
        break;
      case XACTNOTIFICATIONTYPE_WAVEPREPARED:
      case XACTNOTIFICATIONTYPE_WAVEPLAY:
      case XACTNOTIFICATIONTYPE_WAVESTOP:
      case XACTNOTIFICATIONTYPE_WAVELOOPED:
      case XACTNOTIFICATIONTYPE_WAVEDESTROYED:
        out.wave.pWaveBank = waveBank(note->wave.pWaveBank);
        out.wave.waveIndex = note->wave.waveIndex;
        out.wave.cueIndex = note->wave.cueIndex;
        out.wave.pSoundBank = soundBank(note->wave.pSoundBank);
        out.wave.pCue = cue(note->wave.pCue);
        out.wave.pWave = reinterpret_cast<IXACT3Wave*>(note->wave.pWave);
        break;
      default:
        break;
      }
    }
    client(&out);
  }
} // namespace

namespace faf_posix::audio
{
  int CreateXactEngine(const std::uint32_t mode, void** const outEngine) noexcept
  {
    if (outEngine == nullptr) {
      return E_POINTER;
    }
    *outEngine = nullptr;
    if (mode != 0u) {
      FAF_POSIX_STUB("SND: /xactdebug and /audition have no FACT counterpart; using the retail engine");
    }

    FACTAudioEngine* engine = nullptr;
    if (FACTCreateEngine(0u, &engine) != 0u || engine == nullptr) {
      return E_OUTOFMEMORY;
    }
    auto* const adapter = new (std::nothrow) FactEngine(engine);
    if (adapter == nullptr) {
      (void)FACTAudioEngine_Release(engine);
      return E_OUTOFMEMORY;
    }
    *outEngine = static_cast<moho::IXACTEngine*>(adapter);
    return S_OK;
  }
} // namespace faf_posix::audio
