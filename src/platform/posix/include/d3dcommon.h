#pragma once

// Stand-in for the Windows SDK's <d3dcommon.h> on POSIX: value types only (no COM
// interfaces). Values are the SDK's.

typedef struct _D3D_SHADER_MACRO {
  LPCSTR Name;
  LPCSTR Definition;
} D3D_SHADER_MACRO;
