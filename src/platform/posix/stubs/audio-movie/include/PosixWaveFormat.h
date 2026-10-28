#pragma once

// POSIX build (FAF_POSIX): the wave-format structures of the Windows SDK's <mmreg.h> /
// <mmsystem.h> that the audio and movie code uses, at their Windows layouts (both are
// byte-packed there). Shared by the xact3.h and dsound.h stand-ins next to this file.
// Guarded with the SDK's own macros so a second stand-in can coexist.

#if !defined(FAF_POSIX)
#error "PosixWaveFormat.h is for the POSIX build only"
#endif

#if !defined(_WAVEFORMATEX_)
#define _WAVEFORMATEX_
#pragma pack(push, 1)
typedef struct tWAVEFORMATEX
{
  WORD wFormatTag;
  WORD nChannels;
  DWORD nSamplesPerSec;
  DWORD nAvgBytesPerSec;
  WORD nBlockAlign;
  WORD wBitsPerSample;
  WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *NPWAVEFORMATEX, *LPWAVEFORMATEX;
#pragma pack(pop)
typedef const WAVEFORMATEX* LPCWAVEFORMATEX;
static_assert(sizeof(WAVEFORMATEX) == 18, "WAVEFORMATEX is 18 bytes on Windows");
#endif

#if !defined(WAVE_FORMAT_PCM)
#define WAVE_FORMAT_PCM 1
#endif
#if !defined(WAVE_FORMAT_IEEE_FLOAT)
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#if !defined(WAVE_FORMAT_EXTENSIBLE)
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

#if !defined(_WAVEFORMATEXTENSIBLE_)
#define _WAVEFORMATEXTENSIBLE_
#pragma pack(push, 1)
typedef struct
{
  WAVEFORMATEX Format;
  union
  {
    WORD wValidBitsPerSample;
    WORD wSamplesPerBlock;
    WORD wReserved;
  } Samples;
  DWORD dwChannelMask;
  GUID SubFormat;
} WAVEFORMATEXTENSIBLE, *PWAVEFORMATEXTENSIBLE;
#pragma pack(pop)
static_assert(sizeof(WAVEFORMATEXTENSIBLE) == 0x28, "WAVEFORMATEXTENSIBLE is 0x28 bytes on Windows");
#endif

// Speaker positions (ksmedia.h), the bits of WAVEFORMATEXTENSIBLE::dwChannelMask.
#if !defined(SPEAKER_FRONT_LEFT)
#define SPEAKER_FRONT_LEFT 0x1
#define SPEAKER_FRONT_RIGHT 0x2
#define SPEAKER_FRONT_CENTER 0x4
#define SPEAKER_LOW_FREQUENCY 0x8
#define SPEAKER_BACK_LEFT 0x10
#define SPEAKER_BACK_RIGHT 0x20
#define SPEAKER_FRONT_LEFT_OF_CENTER 0x40
#define SPEAKER_FRONT_RIGHT_OF_CENTER 0x80
#define SPEAKER_BACK_CENTER 0x100
#define SPEAKER_SIDE_LEFT 0x200
#define SPEAKER_SIDE_RIGHT 0x400
#endif
