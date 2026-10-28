#pragma once

// POSIX build (FAF_POSIX): the null gal device, `DeviceApi::Null`.
//
// The device the headless replay runner renders "into" (moho::REN_InitHeadless,
// platform/posix/headless/HeadlessRender.h): it lets the unchanged user-side session code run
// without a window or a GPU, the way the Windows parity runner runs on a hidden window.
//
//   - Resources are CPU-backed. A texture locks real memory laid out like the Direct3D 11 /
//     Vulkan backends lay it out (DXGI row pitch from format and size, per mip level; block
//     compressed formats by 4x4 blocks); contents persist between locks and start as zeros.
//     A texture made from a file takes its format, size, mip count and cube/volume shape
//     from the file's header (DDS, PNG, JPEG, BMP, TGA), with no pixel decoding.
//     Vertex and index buffers lock the same CPU copy the other backends hand out
//     (backend::LockShadow). Render and depth-stencil targets carry their contexts only.
//   - Effects accept every technique and variable: the techniques the source declares are
//     listed with their annotations and pass counts (a scan of the source: #if/#ifdef
//     blocks and object-like #defines are honoured, nothing is compiled); any other name
//     still yields a technique or variable whose calls do nothing.
//   - Draws, clears, presents and state changes do nothing. Caps are the Vulkan backend's
//     (D3D10 effect dialect, shader profiles 4/8, hardware instancing). Saving a texture or
//     reading a render target back yields nothing; UpdateSurface copies the CPU texels.
//
// Nothing here is reachable from the simulation; it only replaces the GPU under the
// user-side session (CWldSession::DoBeat -> CWldTerrainRes::NotifyMapChange -> Finalize,
// texture sheets, effect loading in CD3DDeviceResources).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <dxgiformat.h>

#include "boost/shared_ptr.h"
#include "boost/weak_ptr.h"
#include "gpg/core/streams/MemBufferStream.h"
#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/CubeRenderTargetContext.hpp"
#include "gpg/gal/DepthStencilTarget.hpp"
#include "gpg/gal/DepthStencilTargetContext.hpp"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Effect.hpp"
#include "gpg/gal/EffectContext.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/EffectVariable.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/IndexBufferContext.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "gpg/gal/PipelineState.hpp"
#include "gpg/gal/RenderTarget.hpp"
#include "gpg/gal/RenderTargetContext.hpp"
#include "gpg/gal/Texture.hpp"
#include "gpg/gal/TextureContext.h"
#include "gpg/gal/VertexBuffer.hpp"
#include "gpg/gal/VertexBufferContext.hpp"
#include "gpg/gal/VertexFormat.hpp"
#include "gpg/gal/backends/common/LockShadow.h"
#include "gpg/gal/effects/EffectDescription.h"
#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"

namespace gpg::gal
{
  namespace null_device
  {
    /** What a texture file's header says; no pixels are decoded. */
    struct ImageInfo
    {
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      std::uint32_t depth = 1;     // volume textures
      std::uint32_t mipLevels = 1;
      std::uint32_t arraySize = 1; // 6 for a cube
      bool cube = false;
      bool volume = false;
    };

    /**
     * Reads the size and layout of a DDS, PNG, JPEG, BMP or TGA file in memory. Formats
     * other than DDS come back as DXGI_FORMAT_B8G8R8A8_UNORM with a full mip chain, as
     * D3DX loads them. False when the data is none of these.
     */
    [[nodiscard]] bool ReadImageInfo(const void* data, std::size_t bytes, ImageInfo& out);

    /** Row pitch and row count of one level; unknown formats count 4 bytes a texel (gal 1, R8G8B8: 3). */
    struct LevelLayout
    {
      std::uint32_t rowPitch = 0;
      std::uint32_t rowCount = 0;
      [[nodiscard]] std::size_t ByteSize() const noexcept { return static_cast<std::size_t>(rowPitch) * rowCount; }
    };
    [[nodiscard]] LevelLayout ComputeLevelLayout(DXGI_FORMAT format, std::uint32_t galFormat, std::uint32_t width, std::uint32_t height) noexcept;

    /** One technique an effect source declares. */
    struct TechniqueInfo
    {
      std::string name;
      effects::Annotations annotations;
      int passCount = 0;
    };

    /**
     * The techniques of an effect source (the engine's compatibility preamble plus the
     * .fx file, as CD3DEffect merges them), with `macros` predefined. Never throws on odd
     * input: what it cannot read it leaves out.
     */
    [[nodiscard]] std::vector<TechniqueInfo> ScanEffectTechniques(std::string_view source, const msvc8::vector<EffectMacro>& macros);
  } // namespace null_device

  class TextureNull final : public Texture
  {
  public:
    /** `info`: the shape of the texture (from the context, or from the file's header). */
    TextureNull(const TextureContext& context, const null_device::ImageInfo& info);
    ~TextureNull() override;

    TextureContext* GetContext() override;
    TextureLockRect Lock(int level, const RECT& rect, int flags) override;
    int Unlock(TextureLockRect lock) override;
    int Unlock(int level) override;
    void SaveToBuffer(gpg::MemBuffer<char>* outBuffer) override;

    [[nodiscard]] DXGI_FORMAT DxgiFormat() const noexcept { return mInfo.format; }
    [[nodiscard]] const null_device::ImageInfo& Info() const noexcept { return mInfo; }
    [[nodiscard]] null_device::LevelLayout LayoutOf(std::uint32_t level) const noexcept;
    /** The CPU texels of `level` (of the first face / slice), allocated as zeros on first use. */
    [[nodiscard]] std::vector<std::uint8_t>& Level(std::uint32_t level);

  private:
    TextureContext mContext;
    null_device::ImageInfo mInfo;
    std::uint32_t mGalFormat = 0;
    std::vector<std::vector<std::uint8_t>> mLevels;
    bool mLocked = false;
    int mLockedLevel = 0;
  };

  class RenderTargetNull final : public RenderTarget
  {
  public:
    explicit RenderTargetNull(const RenderTargetContext& context);
    ~RenderTargetNull() override;

    RenderTargetContext* GetContext() override;
    HDC GetDC() override;

  private:
    RenderTargetContext mContext;
  };

  class DepthStencilTargetNull final : public DepthStencilTarget
  {
  public:
    explicit DepthStencilTargetNull(const DepthStencilTargetContext& context);
    ~DepthStencilTargetNull() override;

    DepthStencilTargetContext* GetContext() override;

  private:
    DepthStencilTargetContext mContext;
  };

  class CubeRenderTargetNull final : public CubeRenderTarget
  {
  public:
    explicit CubeRenderTargetNull(const CubeRenderTargetContext& context);
    ~CubeRenderTargetNull() override;

    CubeRenderTargetContext* GetContext() override;

  private:
    CubeRenderTargetContext mContext;
  };

  /** CPU storage with the other backends' lock rules (see BufferStorageVulkan). */
  class BufferStorageNull
  {
  public:
    BufferStorageNull(std::uint32_t byteWidth, const char* kind);

    BufferStorageNull(const BufferStorageNull&) = delete;
    BufferStorageNull& operator=(const BufferStorageNull&) = delete;

    [[nodiscard]] std::uint8_t* Lock(std::uint32_t offset, std::uint32_t size);
    void Unlock();
    [[nodiscard]] std::uint32_t ByteWidth() const noexcept { return mByteWidth; }
    [[nodiscard]] const std::uint8_t* Data() const noexcept { return mShadow.data(); }

  private:
    std::uint32_t mByteWidth = 0;
    const char* mKind = nullptr;
    backend::LockShadow mShadow;
    bool mLocked = false;
  };

  class VertexBufferNull final : public VertexBuffer
  {
  public:
    explicit VertexBufferNull(const VertexBufferContext& context);
    ~VertexBufferNull() override;

    VertexBufferContext* GetContext() override;
    void* Lock(unsigned int offset, unsigned int size, MohoD3DLockFlags lockFlags) override;
    void Unlock() override;

    [[nodiscard]] BufferStorageNull& Storage() noexcept { return mStorage; }

  private:
    VertexBufferContext mContext;
    BufferStorageNull mStorage;
  };

  class IndexBufferNull final : public IndexBuffer
  {
  public:
    explicit IndexBufferNull(const IndexBufferContext& context);
    ~IndexBufferNull() override;

    IndexBufferContext* GetContext() override;
    std::int16_t* Lock(unsigned int offset, unsigned int size, MohoD3DLockFlags lockFlags) override;
    void Unlock() override;

    [[nodiscard]] BufferStorageNull& Storage() noexcept { return mStorage; }

  private:
    IndexBufferContext mContext;
    BufferStorageNull mStorage;
  };

  class VertexFormatNull final : public VertexFormat
  {
  public:
    explicit VertexFormatNull(std::uint32_t formatCode);
    ~VertexFormatNull() override;
  };

  class PipelineStateNull final : public PipelineState
  {
  public:
    PipelineStateNull() = default;
    ~PipelineStateNull() override;
  };

  class EffectTechniqueNull final : public EffectTechnique
  {
  public:
    /** `info` null: a technique the source does not declare (one pass, no annotations). */
    EffectTechniqueNull(const char* name, std::shared_ptr<const null_device::TechniqueInfo> info);
    ~EffectTechniqueNull() override;

    msvc8::string* GetName() override;
    int BeginTechnique() override;
    void EndTechnique() override;
    void BeginPass(int pass) override;
    void EndPass() override;
    bool GetAnnotationBool(bool* outValue, const msvc8::string& annotationName) override;
    bool GetAnnotationInt(int* outValue, const msvc8::string& annotationName) override;
    bool GetAnnotationFloat(float* outValue, const msvc8::string& annotationName) override;
    bool GetAnnotationString(msvc8::string* outValue, const msvc8::string& annotationName) override;

  private:
    [[nodiscard]] const effects::Annotation* Find(const msvc8::string& annotationName) const;

    msvc8::string mName;
    std::shared_ptr<const null_device::TechniqueInfo> mInfo;
  };

  class EffectVariableNull final : public EffectVariable
  {
  public:
    explicit EffectVariableNull(const char* name);
    ~EffectVariableNull() override;

    msvc8::string* GetName() override;
    void SetCubeRenderTarget(boost::shared_ptr<CubeRenderTarget> cubeTarget) override;
    void SetRenderTarget(boost::shared_ptr<RenderTarget> renderTarget) override;
    void SetTexture(boost::shared_ptr<Texture> texture) override;
    void SetMatrix4x4(const Matrix* matrix) override;
    void SetFloatArray(std::uint32_t count, const float* values) override;
    void SetVector(const float* vector4) override;
    void SetValue(const void* data, std::uint32_t byteCount) override;
    void SetFloat(float value) override;
    void SetInt(int value) override;
    void SetBool(bool value) override;
    void SetMatrixArray(std::uint32_t count, const Matrix* matrices) override;
    void SetVectorArray(std::uint32_t count, const float* vectors4) override;
    bool GetAnnotationBool(bool* outValue, const msvc8::string& annotationName) override;
    bool GetAnnotationInt(int* outValue, const msvc8::string& annotationName) override;
    bool GetAnnotationFloat(float* outValue, const msvc8::string& annotationName) override;
    bool GetAnnotationString(msvc8::string* outValue, const msvc8::string& annotationName) override;

  private:
    msvc8::string mName;
  };

  class EffectNull final : public Effect
  {
  public:
    EffectNull(const EffectContext& context, std::vector<null_device::TechniqueInfo> techniques);
    ~EffectNull() override;

    EffectContext* GetContext() override;
    void GetTechniques(msvc8::vector<boost::shared_ptr<EffectTechnique>>& outTechniques) override;
    boost::shared_ptr<EffectVariable> GetVariable(const char* name) override;
    boost::shared_ptr<EffectTechnique> GetTechnique(const char* name) override;
    void OnReset() override;
    void OnLost() override;

  private:
    EffectContext mContext;
    std::vector<std::shared_ptr<const null_device::TechniqueInfo>> mTechniques;
  };

  class DeviceNull final : public Device
  {
  public:
    DeviceNull();
    ~DeviceNull() override;

    /** Takes `context` (its heads; a head of size 0 becomes 1024x768) and builds the back buffers. */
    void Setup(DeviceContext* context);

    void* GetLog() override;
    DeviceContext* GetDeviceContext() override;
    int GetCurThreadId() override;
    void Func1() const override;
    void GetModesForAdapter(msvc8::vector<HeadAdapterMode>& outModes, int adapterIndex) override;
    OutputContext* GetHeadOutputContext(unsigned int headIndex) override;
    const OutputContext* GetHeadOutputContext(unsigned int headIndex) const override;
    boost::shared_ptr<PipelineState> GetPipelineState() override;
    boost::shared_ptr<Effect> CreateEffect(const EffectContext& context) override;
    boost::shared_ptr<Texture> CreateTexture(const TextureContext* context) override;
    boost::shared_ptr<RenderTarget> CreateRenderTarget(const RenderTargetContext* context) override;
    boost::shared_ptr<CubeRenderTarget> CreateCubeRenderTarget(const CubeRenderTargetContext* context) override;
    boost::shared_ptr<DepthStencilTarget> CreateDepthStencilTarget(const DepthStencilTargetContext* context) override;
    boost::shared_ptr<VertexFormat> CreateVertexFormat(std::uint32_t formatCode) override;
    boost::shared_ptr<VertexBuffer> CreateVertexBuffer(const VertexBufferContext* context) override;
    boost::shared_ptr<IndexBuffer> CreateIndexBuffer(const IndexBufferContext* context) override;
    void GetRenderTargetData(const boost::shared_ptr<RenderTarget>& source, const boost::shared_ptr<Texture>& destination) override;
    void StretchRect(
      const boost::shared_ptr<RenderTarget>& source,
      const boost::shared_ptr<RenderTarget>& destination,
      const RECT* sourceRect,
      const RECT* destinationRect
    ) override;
    void UpdateSurface(
      const boost::shared_ptr<Texture>& source,
      const boost::shared_ptr<Texture>& destination,
      const RECT* sourceRect,
      const RECT* destinationRect
    ) override;
    void SaveCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget>& cubeTarget, const msvc8::string& filePath) override;
    void SaveRenderTarget(const boost::shared_ptr<RenderTarget>& renderTarget, const msvc8::string& filePath, int fileFormat) override;
    void SaveTexture(
      const boost::shared_ptr<Texture>& texture, const msvc8::string& filePath, int fileFormat, gpg::MemBuffer<char>* outBuffer
    ) override;
    void GetTexture2D(
      const void* sourceData, std::uint32_t sourceBytes, gpg::MemBuffer<char>* outTextureData, std::uint32_t* outWidth, int* outHeight
    ) override;
    boost::weak_ptr<void>* Func7(boost::weak_ptr<void>* outWeakHandle, boost::shared_ptr<void> temporarySharedHandle) override;
    void Reset(DeviceContext* context) override;
    void Reset() override;
    int TestCooperativeLevel() override;
    void BeginScene() override;
    void EndScene() override;
    void Present() override;
    void SetCursor(const CursorContext* context) override;
    void InitCursor() override;
    int ShowCursor(bool show) override;
    void SetViewport(const D3DVIEWPORT9* viewport) override;
    void GetViewport(D3DVIEWPORT9* outViewport) override;
    void ClearTarget(const OutputContext* context) override;
    void Clear(bool clearColor, bool clearDepth, bool clearStencil, std::uint32_t packedColor, float depth, int stencil) override;
    void ClearTextures() override;
    void SetVertexDeclaration(boost::shared_ptr<VertexFormat> vertexFormat) override;
    void SetVertexBuffer(
      std::uint32_t streamSlot, boost::shared_ptr<VertexBuffer> vertexBuffer, int streamFrequencyToken, int startVertex
    ) override;
    void SetBufferIndices(boost::shared_ptr<IndexBuffer> indexBuffer) override;
    void SetFogState(bool enable, const Matrix* projection, float fogStart, float fogEnd, int fogColor) override;
    void SetWireframeState(bool enabled) override;
    void SetColorWriteState(bool writeColor, bool writeAlpha) override;
    void DrawIndexedPrimitive(const DrawIndexedContext* context) override;
    void DrawPrimitive(const DrawContext* context) override;
    void BeginTechnique() override;
    void EndTechnique() override;
    bool GetRuntimeInfo(DeviceRuntimeInfo& out) override;

    /** Every texture format the null device makes (as the Vulkan backend on a full-featured GPU). */
    [[nodiscard]] bool SupportsVertexTextureFormat(std::uint32_t textureFormat);

  private:
    void CreateHeadTargets();

    int mCurThreadId = 0;
    msvc8::vector<msvc8::string> mLog{};
    DeviceContext mDeviceContext{DeviceApi::Unset};
    std::vector<OutputContext> mHeadOutputContexts;
    boost::shared_ptr<PipelineStateNull> mPipelineState;
    D3DVIEWPORT9 mViewport{};
    bool mCursorShown = true;
  };
} // namespace gpg::gal
