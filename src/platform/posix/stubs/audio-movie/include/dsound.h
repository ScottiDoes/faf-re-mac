#pragma once

// POSIX build (FAF_POSIX): stand-in for the Windows SDK <dsound.h>.
//
// DirectSound is Windows-only. The engine opens it in two places: func_InitSound
// (moho/audio/AudioEngine.cpp) asks it for the speaker layout, and CMovieManager
// (moho/misc/StartupHelpers.cpp) hands a device to the movie player. Both already
// handle a machine without DirectSound ("Failed to create DirectSound." and carry on
// without sound). On POSIX DirectSoundCreate is that machine: it fails with
// DSERR_NODRIVER (platform/posix/stubs/audio-movie/DirectSoundPosix.cpp), so no
// IDirectSound object ever exists here. The interfaces below are declared in the
// SDK's vtable order so the calling code compiles unchanged.

#if !defined(FAF_POSIX)
#error "This dsound.h is the POSIX stand-in; Windows builds use the Windows SDK"
#endif

#include "PosixWaveFormat.h"

struct IDirectSound;
struct IDirectSoundBuffer;
typedef struct IDirectSound* LPDIRECTSOUND;
typedef struct IDirectSoundBuffer* LPDIRECTSOUNDBUFFER;

// --- Results ---------------------------------------------------------------------------
#define _FACDS 0x878
#define MAKE_DSHRESULT(code) ((HRESULT)(0x80000000u | (_FACDS << 16) | (code)))
#define DS_OK S_OK
#define DSERR_ALLOCATED MAKE_DSHRESULT(10)
#define DSERR_CONTROLUNAVAIL MAKE_DSHRESULT(30)
#define DSERR_INVALIDPARAM E_INVALIDARG
#define DSERR_INVALIDCALL MAKE_DSHRESULT(50)
#define DSERR_GENERIC E_FAIL
#define DSERR_PRIOLEVELNEEDED MAKE_DSHRESULT(70)
#define DSERR_OUTOFMEMORY E_OUTOFMEMORY
#define DSERR_BADFORMAT MAKE_DSHRESULT(100)
#define DSERR_UNSUPPORTED E_NOTIMPL
#define DSERR_NODRIVER MAKE_DSHRESULT(120)
#define DSERR_ALREADYINITIALIZED MAKE_DSHRESULT(130)
#define DSERR_BUFFERLOST MAKE_DSHRESULT(150)
#define DSERR_OTHERAPPHASPRIO MAKE_DSHRESULT(160)
#define DSERR_UNINITIALIZED MAKE_DSHRESULT(170)

// --- Cooperative levels, buffer caps, play/status flags, volume range -----------------
#define DSSCL_NORMAL 0x00000001
#define DSSCL_PRIORITY 0x00000002
#define DSSCL_EXCLUSIVE 0x00000003
#define DSSCL_WRITEPRIMARY 0x00000004

#define DSBCAPS_PRIMARYBUFFER 0x00000001
#define DSBCAPS_STATIC 0x00000002
#define DSBCAPS_LOCHARDWARE 0x00000004
#define DSBCAPS_LOCSOFTWARE 0x00000008
#define DSBCAPS_CTRL3D 0x00000010
#define DSBCAPS_CTRLFREQUENCY 0x00000020
#define DSBCAPS_CTRLPAN 0x00000040
#define DSBCAPS_CTRLVOLUME 0x00000080
#define DSBCAPS_CTRLPOSITIONNOTIFY 0x00000100
#define DSBCAPS_CTRLFX 0x00000200
#define DSBCAPS_STICKYFOCUS 0x00004000
#define DSBCAPS_GLOBALFOCUS 0x00008000
#define DSBCAPS_GETCURRENTPOSITION2 0x00010000
#define DSBCAPS_MUTE3DATMAXDISTANCE 0x00020000
#define DSBCAPS_LOCDEFER 0x00040000

#define DSBPLAY_LOOPING 0x00000001
#define DSBSTATUS_PLAYING 0x00000001
#define DSBSTATUS_BUFFERLOST 0x00000002
#define DSBSTATUS_LOOPING 0x00000004
#define DSBLOCK_FROMWRITECURSOR 0x00000001
#define DSBLOCK_ENTIREBUFFER 0x00000002

#define DSBVOLUME_MIN -10000
#define DSBVOLUME_MAX 0

// --- Speaker configurations (IDirectSound::GetSpeakerConfig) --------------------------
#define DSSPEAKER_DIRECTOUT 0x00000000
#define DSSPEAKER_HEADPHONE 0x00000001
#define DSSPEAKER_MONO 0x00000002
#define DSSPEAKER_QUAD 0x00000003
#define DSSPEAKER_STEREO 0x00000004
#define DSSPEAKER_SURROUND 0x00000005
#define DSSPEAKER_5POINT1 0x00000006
#define DSSPEAKER_7POINT1 0x00000007
#define DSSPEAKER_7POINT1_SURROUND 0x00000008
#define DSSPEAKER_5POINT1_SURROUND 0x00000009
#define DSSPEAKER_CONFIG(a) ((BYTE)(a))

// --- Structures --------------------------------------------------------------------------
typedef struct _DSBUFFERDESC
{
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwBufferBytes;
  DWORD dwReserved;
  LPWAVEFORMATEX lpwfxFormat;
  GUID guid3DAlgorithm;
} DSBUFFERDESC, *LPDSBUFFERDESC;
typedef const DSBUFFERDESC* LPCDSBUFFERDESC;

typedef struct _DSCAPS
{
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwMinSecondarySampleRate;
  DWORD dwMaxSecondarySampleRate;
  DWORD dwPrimaryBuffers;
  DWORD dwMaxHwMixingAllBuffers;
  DWORD dwMaxHwMixingStaticBuffers;
  DWORD dwMaxHwMixingStreamingBuffers;
  DWORD dwFreeHwMixingAllBuffers;
  DWORD dwFreeHwMixingStaticBuffers;
  DWORD dwFreeHwMixingStreamingBuffers;
  DWORD dwMaxHw3DAllBuffers;
  DWORD dwMaxHw3DStaticBuffers;
  DWORD dwMaxHw3DStreamingBuffers;
  DWORD dwFreeHw3DAllBuffers;
  DWORD dwFreeHw3DStaticBuffers;
  DWORD dwFreeHw3DStreamingBuffers;
  DWORD dwTotalHwMemBytes;
  DWORD dwFreeHwMemBytes;
  DWORD dwMaxContigFreeHwMemBytes;
  DWORD dwUnlockTransferRateHwBuffers;
  DWORD dwPlayCpuOverheadSwBuffers;
  DWORD dwReserved1;
  DWORD dwReserved2;
} DSCAPS, *LPDSCAPS;

typedef struct _DSBCAPS
{
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwBufferBytes;
  DWORD dwUnlockTransferRate;
  DWORD dwPlayCpuOverhead;
} DSBCAPS, *LPDSBCAPS;

// --- Interfaces (SDK vtable order; IUnknown's three slots first) ----------------------
struct IDirectSound
{
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
  virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG STDMETHODCALLTYPE Release() = 0;
  virtual HRESULT STDMETHODCALLTYPE
  CreateSoundBuffer(LPCDSBUFFERDESC pcDSBufferDesc, LPDIRECTSOUNDBUFFER* ppDSBuffer, void* pUnkOuter) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS pDSCaps) = 0;
  virtual HRESULT STDMETHODCALLTYPE
  DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pDSBufferOriginal, LPDIRECTSOUNDBUFFER* ppDSBufferDuplicate) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd, DWORD dwLevel) = 0;
  virtual HRESULT STDMETHODCALLTYPE Compact() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD pdwSpeakerConfig) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD dwSpeakerConfig) = 0;
  virtual HRESULT STDMETHODCALLTYPE Initialize(const GUID* pcGuidDevice) = 0;
};

struct IDirectSoundBuffer
{
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
  virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG STDMETHODCALLTYPE Release() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS pDSBufferCaps) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD pdwCurrentPlayCursor, LPDWORD pdwCurrentWriteCursor) = 0;
  virtual HRESULT STDMETHODCALLTYPE
  GetFormat(LPWAVEFORMATEX pwfxFormat, DWORD dwSizeAllocated, LPDWORD pdwSizeWritten) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetVolume(LPLONG plVolume) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPan(LPLONG plPan) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD pdwFrequency) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD pdwStatus) = 0;
  virtual HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND pDirectSound, LPCDSBUFFERDESC pcDSBufferDesc) = 0;
  virtual HRESULT STDMETHODCALLTYPE Lock(
    DWORD dwOffset,
    DWORD dwBytes,
    LPVOID* ppvAudioPtr1,
    LPDWORD pdwAudioBytes1,
    LPVOID* ppvAudioPtr2,
    LPDWORD pdwAudioBytes2,
    DWORD dwFlags
  ) = 0;
  virtual HRESULT STDMETHODCALLTYPE Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD dwNewPosition) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX pcfxFormat) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetVolume(LONG lVolume) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPan(LONG lPan) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetFrequency(DWORD dwFrequency) = 0;
  virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
  virtual HRESULT STDMETHODCALLTYPE
  Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioBytes2) = 0;
  virtual HRESULT STDMETHODCALLTYPE Restore() = 0;
};

// --- Entry point ---------------------------------------------------------------------------
// POSIX: always fails with DSERR_NODRIVER and sets *ppDS to null (see the top of this file).
extern "C" HRESULT WINAPI DirectSoundCreate(const GUID* pcGuidDevice, LPDIRECTSOUND* ppDS, void* pUnkOuter);
