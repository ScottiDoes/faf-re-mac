# Package "render" (gpg/gal, moho/render, moho/mesh, moho/particles): translation units
# left out of the macOS build, each with what replaces it. Notes: macos/porting/render.md.
# (The Direct3D 9/10/11/12 and DXGI backends are excluded by macos/exclusions.cmake.)

# DXC front end (dxcompiler.dll/dxil.dll through WRL ComPtr and ID3D12ShaderReflection).
# Replaced by platform/posix/stubs/render/gal/ShaderCompilerPosix.cpp: the same front end on
# libdxcompiler.dylib through the DXC bridge (platform/posix/dxc); SPIR-V only, DXIL reported
# unavailable (macos/porting/shaders.md).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^gpg/gal/effects/ShaderCompiler\\.cpp$=>platform/posix/stubs/render/gal/ShaderCompilerPosix.cpp")

# Texture file codec running D3DX11 on a WARP Direct3D 11 device.
# Replaced by platform/posix/stubs/render/gal/ImageCodecPosix.cpp: the POSIX codec (DDS in
# C++, ImageIO for PNG/JPEG/BMP/TGA, in-house BC1-BC5, the engine's DDS direct path and
# decode cache; macos/porting/textures.md).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^gpg/gal/backends/common/ImageCodec\\.cpp$=>platform/posix/stubs/render/gal/ImageCodecPosix.cpp")

# DirectXTex decoder and encoder for ImageCodec.cpp without d3dx11_43.dll (issue #318).
# Its only caller is ImageCodec.cpp, replaced above; nothing replaces it.
list(APPEND FAF_MACOS_EXCLUDED_SOURCES "^gpg/gal/backends/common/ImageCodecDirectXTex\\.cpp$")

# Win32 colour cursor (GDI DIB + CreateIconIndirect + user32 SetCursor/ShowCursor).
# Replaced by platform/posix/stubs/render/gal/WindowsCursorPosix.cpp: real colour cursors
# through the shell (SDL3; macos/porting/shell.md).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^gpg/gal/backends/common/WindowsCursor\\.cpp$=>platform/posix/stubs/render/gal/WindowsCursorPosix.cpp")

# The Vulkan backend's window-system unit (VK_KHR_win32_surface). Replaced by
# platform/posix/stubs/render/gal/VulkanSurfacePosix.cpp: VK_EXT_metal_surface on the
# window's CAMetalLayer (faf_posix::shell::MetalLayer; macos/porting/vulkan.md).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^gpg/gal/backends/vulkan/VulkanSurface\\.cpp$=>platform/posix/stubs/render/gal/VulkanSurfacePosix.cpp")

# DXGI adapter/memory queries for ren_Info; its only callers are the Direct3D 9/11/12
# backends, which are excluded. Nothing replaces it.
list(APPEND FAF_MACOS_EXCLUDED_SOURCES "^gpg/gal/DxgiAdapterInfo\\.cpp$")

# GDI glyph rasterizer behind every in-game font. Replaced by
# platform/posix/stubs/render/moho/CD3DFontPosix.cpp, which compiles the engine's own
# CD3DFont.cpp unchanged on the Gdi area's Core Text fonts (macos/porting/fonts.md).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^moho/render/d3d/CD3DFont\\.cpp$=>platform/posix/stubs/render/moho/CD3DFontPosix.cpp")
