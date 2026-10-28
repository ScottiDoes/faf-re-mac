#pragma once

// Stand-in for the Windows SDK's <d3d11.h> on POSIX: only the value types that
// renderer-neutral code shares with the Direct3D 11 backend, which is not built here.
// The Vulkan backend reads the gal vertex layouts as D3D11 input elements
// (gpg/gal/backends/d3d11/D3D11VertexLayouts). No COM interfaces, no functions:
// code that needs the real API must stay behind `!defined(FAF_POSIX)`.
// Values are the SDK's.

#include <dxgiformat.h>
#include <d3dcommon.h>

typedef enum D3D11_INPUT_CLASSIFICATION {
  D3D11_INPUT_PER_VERTEX_DATA = 0,
  D3D11_INPUT_PER_INSTANCE_DATA = 1
} D3D11_INPUT_CLASSIFICATION;

#define D3D11_APPEND_ALIGNED_ELEMENT (0xffffffff)

typedef struct D3D11_INPUT_ELEMENT_DESC {
  LPCSTR SemanticName;
  UINT SemanticIndex;
  DXGI_FORMAT Format;
  UINT InputSlot;
  UINT AlignedByteOffset;
  D3D11_INPUT_CLASSIFICATION InputSlotClass;
  UINT InstanceDataStepRate;
} D3D11_INPUT_ELEMENT_DESC;
