// POSIX build: the Direct3D 10 hardware vertex formatters, which every API but Direct3D 9
// uses (gpg/gal/MeshVertex.cpp: Direct3D 10/11/12 and Vulkan pick them), are defined in
// gpg/gal/backends/d3d10/D3D10Interfaces.cpp - excluded here with the rest of the
// Direct3D 10 backend. These are the same bodies (D3D10Interfaces.cpp, 0x0094D4F0 ..
// 0x0094D7E0), with D3DXFloat32To16Array replaced by a portable conversion.
//
// Rendering only (mesh instance buffers): nothing here feeds the simulation.

#include <cstdint>
#include <cstring>

#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/MeshVertex.h"
#include "gpg/gal/VertexFormat.hpp"
#include "gpg/gal/backends/d3d10/Float16HardwareVertexFormatterD3D10.hpp"
#include "gpg/gal/backends/d3d10/HardwareVertexFormatterD3D10.hpp"

namespace gpg::gal
{
  namespace
  {
    constexpr std::uint32_t kHardwareVertexFormatToken = 14U;
    constexpr std::uint32_t kFloat16VertexFormatToken = 15U;
    constexpr std::uint32_t kHardwareVertexStrideBase = 0x48U;
    constexpr std::uint32_t kFloat16VertexStrideStream0 = 0x2CU;
    constexpr std::uint32_t kFloat16VertexStrideStream1 = 0x44U;

    /** D3D10Interfaces.cpp 0x0094D490: rows (0,1,2) of each of the four matrix rows. */
    const Matrix* CopyMatrix4x3Rows(
      float* const outRow0, float* const outRow1, float* const outRow2, float* const outRow3, const Matrix& matrix
    )
    {
      float* const outRows[4] = {outRow0, outRow1, outRow2, outRow3};
      for (int row = 0; row < 4; ++row) {
        outRows[row][0] = matrix.r[row].x;
        outRows[row][1] = matrix.r[row].y;
        outRows[row][2] = matrix.r[row].z;
      }
      return &matrix;
    }

    /**
     * One float to IEEE half, rounding to nearest even (what D3DXFloat32To16Array
     * does for the finite values a mesh instance carries).
     * FAF_POSIX_TODO(milestone 2): confirm d3dx9's handling of overflow/NaN if a shader
     * ever depends on it; render-only, never simulation state.
     */
    [[nodiscard]] std::uint16_t FloatToHalf(const float value) noexcept
    {
      std::uint32_t bits = 0U;
      std::memcpy(&bits, &value, sizeof(bits));
      const std::uint32_t sign = (bits >> 16U) & 0x8000U;
      const std::uint32_t magnitude = bits & 0x7FFFFFFFU;
      if (magnitude >= 0x7F800000U) {
        return static_cast<std::uint16_t>(sign | ((magnitude > 0x7F800000U) ? 0x7E00U : 0x7C00U));
      }
      if (magnitude >= 0x477FF000U) { // rounds past 65504
        return static_cast<std::uint16_t>(sign | 0x7C00U);
      }
      if (magnitude < 0x38800000U) { // below the smallest normal half: denormal or zero
        if (magnitude < 0x33000000U) {
          return static_cast<std::uint16_t>(sign);
        }
        const std::uint32_t exponent = magnitude >> 23U;
        const std::uint32_t mantissa = (magnitude & 0x007FFFFFU) | 0x00800000U;
        const std::uint32_t shift = 126U - exponent; // 14..24
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t remainder = mantissa & ((1U << shift) - 1U);
        const std::uint32_t halfway = 1U << (shift - 1U);
        if (remainder > halfway || (remainder == halfway && (half & 1U) != 0U)) {
          ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
      }
      std::uint32_t half = ((magnitude - 0x38000000U) >> 13U);
      const std::uint32_t remainder = magnitude & 0x1FFFU;
      if (remainder > 0x1000U || (remainder == 0x1000U && (half & 1U) != 0U)) {
        ++half;
      }
      return static_cast<std::uint16_t>(sign | half);
    }
  } // namespace

  HardwareVertexFormatterD3D10::HardwareVertexFormatterD3D10() = default;

  HardwareVertexFormatterD3D10::~HardwareVertexFormatterD3D10() = default;

  bool HardwareVertexFormatterD3D10::AllowMeshInstancing()
  {
    return Device::GetInstance()->GetDeviceContext()->mHWBasedInstancing;
  }

  boost::shared_ptr<VertexFormat> HardwareVertexFormatterD3D10::CreateVertexFormat(const std::int32_t layoutVariant)
  {
    static_cast<void>(layoutVariant);
    return Device::GetInstance()->CreateVertexFormat(kHardwareVertexFormatToken);
  }

  std::uint32_t HardwareVertexFormatterD3D10::GetVertexStride(const std::int32_t streamClass, const std::int32_t sizeVariant)
  {
    static_cast<void>(sizeVariant);
    return kHardwareVertexStrideBase + ((streamClass != 0) ? 4U : 0U);
  }

  void HardwareVertexFormatterD3D10::WriteFormattedVertex(
    const std::int32_t streamClass, void* const destinationVertex, const MeshVertex& source, const std::int32_t writeVariant
  )
  {
    static_cast<void>(writeVariant);

    if (streamClass != 0) {
      auto& destination = *static_cast<HardwareVertexInstance*>(destinationVertex);
      destination.instanceIndex = source.instanceIndex;
      destination.color = source.color;
      destination.shaderTime = source.shaderTime;
      static_cast<void>(CopyMatrix4x3Rows(
        destination.transform[0], destination.transform[1], destination.transform[2], destination.transform[3], source.transform
      ));
      destination.bonePaletteBase = source.bonePaletteBase;
      destination.secondaryDataMask = (source.useSecondaryData != 0U) ? static_cast<std::uint8_t>(0xFFU) : 0U;
      destination.scroll[0] = source.scroll[0];
      destination.scroll[1] = source.scroll[1];
      destination.dissolve = source.dissolve;
      destination.parameter = source.parameter;
      destination.meshColor = source.meshColor;
      return;
    }

    auto& destination = *static_cast<HardwareVertex*>(destinationVertex);
    destination.boneIndices[0] = source.boneIndices[0];
    destination.boneIndices[1] = source.boneIndices[1];
    destination.boneIndices[2] = source.boneIndices[2];
    destination.boneIndices[3] = source.boneIndices[3];
    destination.position[0] = source.position[0];
    destination.position[1] = source.position[1];
    destination.position[2] = source.position[2];
    destination.position[3] = 1.0f;
    destination.normal[0] = source.normal[0];
    destination.normal[1] = source.normal[1];
    destination.normal[2] = source.normal[2];
    destination.binormal[0] = source.binormal[0];
    destination.binormal[1] = source.binormal[1];
    destination.binormal[2] = source.binormal[2];
    destination.tangent[0] = source.tangent[0];
    destination.tangent[1] = source.tangent[1];
    destination.tangent[2] = source.tangent[2];
    destination.texCoords[0] = source.texCoord0[0];
    destination.texCoords[1] = source.texCoord0[1];
    destination.texCoords[2] = source.texCoord1[0];
    destination.texCoords[3] = source.texCoord1[1];
  }

  Float16HardwareVertexFormatterD3D10::Float16HardwareVertexFormatterD3D10() = default;

  Float16HardwareVertexFormatterD3D10::~Float16HardwareVertexFormatterD3D10() = default;

  bool Float16HardwareVertexFormatterD3D10::AllowMeshInstancing()
  {
    const DeviceContext* const context = Device::GetInstance()->GetDeviceContext();
    return context->mHWBasedInstancing && context->mSupportsFloat16;
  }

  boost::shared_ptr<VertexFormat> Float16HardwareVertexFormatterD3D10::CreateVertexFormat(const std::int32_t layoutVariant)
  {
    static_cast<void>(layoutVariant);
    return Device::GetInstance()->CreateVertexFormat(kFloat16VertexFormatToken);
  }

  std::uint32_t Float16HardwareVertexFormatterD3D10::GetVertexStride(const std::int32_t streamClass, const std::int32_t sizeVariant)
  {
    static_cast<void>(sizeVariant);
    return (streamClass != 0) ? kFloat16VertexStrideStream1 : kFloat16VertexStrideStream0;
  }

  void Float16HardwareVertexFormatterD3D10::WriteFormattedVertex(
    const std::int32_t streamClass, void* const destinationVertex, const MeshVertex& source, const std::int32_t writeVariant
  )
  {
    static_cast<void>(streamClass);
    static_cast<void>(writeVariant);

    auto& destination = *static_cast<Float16HardwareVertexInstance*>(destinationVertex);
    destination.instanceIndex = source.instanceIndex;
    destination.color = source.color;
    destination.shaderTime = FloatToHalf(source.shaderTime);
    static_cast<void>(CopyMatrix4x3Rows(
      destination.transform[0], destination.transform[1], destination.transform[2], destination.transform[3], source.transform
    ));
    destination.bonePaletteBase = source.bonePaletteBase;
    destination.secondaryDataMask = (source.useSecondaryData != 0U) ? static_cast<std::uint8_t>(0xFFU) : 0U;
    destination.scroll[1] = FloatToHalf(source.scroll[1]);
    destination.scroll[0] = FloatToHalf(source.scroll[0]);
    destination.dissolve = source.dissolve;
    destination.parameter = FloatToHalf(source.parameter);
    destination.meshColor = source.meshColor;
  }
} // namespace gpg::gal
