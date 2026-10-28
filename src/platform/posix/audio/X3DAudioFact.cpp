// POSIX build: the two X3DAudio (x3daudio1_7.dll) entry points moho/audio/AudioEngine.cpp
// declares, on FAudio's F3DAudio.
//
// func_AudioInitialize calls X3DAudioInitialize with the final mix's speaker mask (from
// IXACTEngine::GetFinalMixFormat, i.e. FACT's mastering voice) and the XACT global
// "SpeedOfSound"; AudioEngine::Calculate3D -> func_X3DAudioCalculate calls
// X3DAudioCalculate with flags 0x61 (matrix, Doppler, emitter angle) for every positioned
// cue, then ApplySettingsToCue hands the matrix and the Distance / DopplerPitchScalar /
// OrientationAngle variables to the cue, as XACT3DApply does.
//
// F3DAudio is X3DAudio's reimplementation with the same arguments and a 20-byte handle.
// The engine's structures (AudioEngine.h) follow the SDK's X3DAUDIO_* field order; they
// are copied field by field into F3DAudio's byte-packed ones rather than cast, so a
// layout difference cannot go unnoticed. Replaces the wave-1 stub
// (stubs/audio-movie/X3DAudioPosix.cpp).

#include <cstdint>
#include <cstring>

#include <F3DAudio.h>

#include "gpg/core/utils/Logging.h"
#include "moho/audio/AudioEngine.h"

namespace
{
  static_assert(sizeof(moho::AudioEngineImpl::mAudioHandle) == F3DAUDIO_HANDLE_BYTESIZE, "X3DAUDIO_HANDLE is 20 bytes");

  // X3DAUDIO_DISTANCE_CURVE as the engine builds it (AudioEngine.cpp's AudioDistanceCurve).
  struct EngineCurvePoint
  {
    float mDistance;
    float mDspSetting;
  };
  struct EngineCurve
  {
    const EngineCurvePoint* mPoints;
    std::uint32_t mPointCount;
  };
  static_assert(sizeof(EngineCurvePoint) == sizeof(F3DAUDIO_DISTANCE_CURVE_POINT), "curve point layout");

  [[nodiscard]] F3DAUDIO_VECTOR ToF3d(const moho::Audio3DVector& v) noexcept
  {
    return F3DAUDIO_VECTOR{v.x, v.y, v.z};
  }

  /// Copies one engine curve (or none) into `storage`; returns what F3DAudio should see.
  [[nodiscard]] F3DAUDIO_DISTANCE_CURVE* ToF3d(const void* const curve, F3DAUDIO_DISTANCE_CURVE& storage) noexcept
  {
    if (curve == nullptr) {
      return nullptr;
    }
    const auto* const engineCurve = static_cast<const EngineCurve*>(curve);
    storage.pPoints = reinterpret_cast<F3DAUDIO_DISTANCE_CURVE_POINT*>(const_cast<EngineCurvePoint*>(engineCurve->mPoints));
    storage.PointCount = engineCurve->mPointCount;
    return &storage;
  }
} // namespace

extern "C" void X3DAudioInitialize(const std::uint32_t speakerChannelMask, const float speedOfSound, void* const instance)
{
  if (instance == nullptr) {
    return;
  }
  const std::uint32_t result = F3DAudioInitialize8(speakerChannelMask, speedOfSound, static_cast<std::uint8_t*>(instance));
  if (result != 0u) {
    // X3DAudioInitialize returns nothing; the engine then calculates with a zeroed
    // handle, as XACT's caller would after a failed X3DAudio initialisation.
    gpg::Warnf(
      "SND: F3DAudioInitialize(mask 0x%x, speed of sound %f) failed (0x%08x)",
      speakerChannelMask,
      static_cast<double>(speedOfSound),
      result
    );
    std::memset(instance, 0, F3DAUDIO_HANDLE_BYTESIZE);
  }
}

extern "C" void X3DAudioCalculate(
  const void* const instance,
  const moho::Audio3DListener* const listener,
  const moho::Audio3DEmitter* const emitter,
  const std::uint32_t flags,
  moho::Audio3DDspSettings* const dspSettings
)
{
  if (instance == nullptr || listener == nullptr || emitter == nullptr || dspSettings == nullptr) {
    return;
  }

  F3DAUDIO_LISTENER f3dListener{};
  f3dListener.OrientFront = ToF3d(listener->mOrientFront);
  f3dListener.OrientTop = ToF3d(listener->mOrientTop);
  f3dListener.Position = ToF3d(listener->mPosition);
  f3dListener.Velocity = ToF3d(listener->mVelocity);
  f3dListener.pCone = static_cast<F3DAUDIO_CONE*>(listener->mCone); // X3DAUDIO_CONE: 8 floats, as F3DAudio's

  F3DAUDIO_DISTANCE_CURVE volume{};
  F3DAUDIO_DISTANCE_CURVE lfe{};
  F3DAUDIO_DISTANCE_CURVE lpfDirect{};
  F3DAUDIO_DISTANCE_CURVE lpfReverb{};
  F3DAUDIO_DISTANCE_CURVE reverb{};
  F3DAUDIO_EMITTER f3dEmitter{};
  f3dEmitter.pCone = static_cast<F3DAUDIO_CONE*>(emitter->mCone);
  f3dEmitter.OrientFront = ToF3d(emitter->mOrientFront);
  f3dEmitter.OrientTop = ToF3d(emitter->mOrientTop);
  f3dEmitter.Position = ToF3d(emitter->mPosition);
  f3dEmitter.Velocity = ToF3d(emitter->mVelocity);
  f3dEmitter.InnerRadius = emitter->mInnerRadius;
  f3dEmitter.InnerRadiusAngle = emitter->mInnerRadiusAngle;
  f3dEmitter.ChannelCount = emitter->mChannelCount;
  f3dEmitter.ChannelRadius = emitter->mChannelRadius;
  f3dEmitter.pChannelAzimuths = emitter->mChannelAzimuths;
  f3dEmitter.pVolumeCurve = ToF3d(emitter->mVolumeCurve, volume);
  f3dEmitter.pLFECurve = ToF3d(emitter->mLfeCurve, lfe);
  f3dEmitter.pLPFDirectCurve = ToF3d(emitter->mLpfDirectCurve, lpfDirect);
  f3dEmitter.pLPFReverbCurve = ToF3d(emitter->mLpfReverbCurve, lpfReverb);
  f3dEmitter.pReverbCurve = ToF3d(emitter->mReverbCurve, reverb);
  f3dEmitter.CurveDistanceScaler = emitter->mCurveDistanceScaler;
  f3dEmitter.DopplerScaler = emitter->mDopplerScaler;

  F3DAUDIO_DSP_SETTINGS f3dSettings{};
  f3dSettings.pMatrixCoefficients = dspSettings->mMatrixCoefficients;
  f3dSettings.pDelayTimes = dspSettings->mDelayTimes;
  f3dSettings.SrcChannelCount = dspSettings->mSrcChannelCount;
  f3dSettings.DstChannelCount = dspSettings->mDstChannelCount;
  f3dSettings.LPFDirectCoefficient = dspSettings->mLpfDirectCoefficient;
  f3dSettings.LPFReverbCoefficient = dspSettings->mLpfReverbCoefficient;
  f3dSettings.ReverbLevel = dspSettings->mReverbLevel;
  f3dSettings.DopplerFactor = dspSettings->mDopplerFactor;
  f3dSettings.EmitterToListenerAngle = dspSettings->mEmitterToListenerAngle;
  f3dSettings.EmitterToListenerDistance = dspSettings->mEmitterToListenerDistance;
  f3dSettings.EmitterVelocityComponent = dspSettings->mEmitterVelocityComponent;
  f3dSettings.ListenerVelocityComponent = dspSettings->mListenerVelocityComponent;

  F3DAudioCalculate(static_cast<const std::uint8_t*>(instance), &f3dListener, &f3dEmitter, flags, &f3dSettings);

  // The matrix and delay arrays were written in place; copy back the scalar results.
  dspSettings->mLpfDirectCoefficient = f3dSettings.LPFDirectCoefficient;
  dspSettings->mLpfReverbCoefficient = f3dSettings.LPFReverbCoefficient;
  dspSettings->mReverbLevel = f3dSettings.ReverbLevel;
  dspSettings->mDopplerFactor = f3dSettings.DopplerFactor;
  dspSettings->mEmitterToListenerAngle = f3dSettings.EmitterToListenerAngle;
  dspSettings->mEmitterToListenerDistance = f3dSettings.EmitterToListenerDistance;
  dspSettings->mEmitterVelocityComponent = f3dSettings.EmitterVelocityComponent;
  dspSettings->mListenerVelocityComponent = f3dSettings.ListenerVelocityComponent;
}
