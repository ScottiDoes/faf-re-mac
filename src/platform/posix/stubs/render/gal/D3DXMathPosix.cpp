// POSIX build: the D3DX 9 matrix functions the engine imports from d3dx9_*.dll on
// Windows (declared extern "C" by gpg/gal/Matrix.cpp and moho/render/Shadow.cpp, not
// through the D3DX headers). Plain implementations of the documented D3DX formulas:
// row-vector convention, row-major storage (moho::VMatrix4 is layout-identical to
// D3DXMATRIX).
//
// Their only callers are the renderer and effects (gal::Math via moho::VMatrix4's
// Load*/VEC_Mul helpers, Shadow, CEfxEmitter, CWldTerrainDecal): no simulation code
// reaches them, so the results need not be bit-identical to d3dx9's CPU-specific
// (SSE/x87) code paths. FAF_POSIX_TODO(determinism): if a sim caller of
// gpg::gal::Math or the VMatrix4 Load*/VEC_Mul helpers ever appears, this file must
// reproduce d3dx9's exact results instead.

#include <cmath>

#include "moho/math/VMatrix4.h"
#include "Wm3Quaternion.h"
#include "Wm3Vector3.h"

namespace
{
  using moho::VMatrix4;

  [[nodiscard]] float& At(VMatrix4& matrix, const int row, const int column) noexcept
  {
    return (&matrix.r[row].x)[column];
  }

  [[nodiscard]] float At(const VMatrix4& matrix, const int row, const int column) noexcept
  {
    return (&matrix.r[row].x)[column];
  }

  void LoadIdentity(VMatrix4& matrix) noexcept
  {
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        At(matrix, row, column) = (row == column) ? 1.0f : 0.0f;
      }
    }
  }

  struct Vec3
  {
    float x, y, z;
  };

  [[nodiscard]] Vec3 Normalize(const Vec3 v) noexcept
  {
    const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0.0f) {
      return Vec3{0.0f, 0.0f, 0.0f};
    }
    return Vec3{v.x / length, v.y / length, v.z / length};
  }

  [[nodiscard]] Vec3 Cross(const Vec3 a, const Vec3 b) noexcept
  {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  }

  [[nodiscard]] float Dot(const Vec3 a, const Vec3 b) noexcept
  {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }
} // namespace

extern "C"
{
  moho::VMatrix4* D3DXMatrixMultiply(moho::VMatrix4* const outMatrix, const moho::VMatrix4* const lhs, const moho::VMatrix4* const rhs)
  {
    VMatrix4 product{};
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        At(product, row, column) = At(*lhs, row, 0) * At(*rhs, 0, column) + At(*lhs, row, 1) * At(*rhs, 1, column) +
          At(*lhs, row, 2) * At(*rhs, 2, column) + At(*lhs, row, 3) * At(*rhs, 3, column);
      }
    }
    *outMatrix = product;
    return outMatrix;
  }

  // D3DX: null (and the output untouched) when the matrix is singular.
  moho::VMatrix4* D3DXMatrixInverse(moho::VMatrix4* const outMatrix, float* const determinantOut, const moho::VMatrix4* const source)
  {
    float m[16];
    for (int index = 0; index < 16; ++index) {
      m[index] = At(*source, index / 4, index % 4);
    }
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
      m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
      m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] -
      m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] +
      m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
      m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
      m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
      m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] -
      m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] -
      m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] +
      m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] -
      m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] +
      m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] +
      m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] -
      m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] +
      m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
      m[8] * m[2] * m[5];

    const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (determinantOut != nullptr) {
      *determinantOut = determinant;
    }
    if (determinant == 0.0f) {
      return nullptr;
    }
    const float scale = 1.0f / determinant;
    for (int index = 0; index < 16; ++index) {
      At(*outMatrix, index / 4, index % 4) = inv[index] * scale;
    }
    return outMatrix;
  }

  moho::VMatrix4* D3DXMatrixTranslation(moho::VMatrix4* const outMatrix, const float x, const float y, const float z)
  {
    LoadIdentity(*outMatrix);
    At(*outMatrix, 3, 0) = x;
    At(*outMatrix, 3, 1) = y;
    At(*outMatrix, 3, 2) = z;
    return outMatrix;
  }

  moho::VMatrix4* D3DXMatrixScaling(moho::VMatrix4* const outMatrix, const float x, const float y, const float z)
  {
    LoadIdentity(*outMatrix);
    At(*outMatrix, 0, 0) = x;
    At(*outMatrix, 1, 1) = y;
    At(*outMatrix, 2, 2) = z;
    return outMatrix;
  }

  moho::VMatrix4* D3DXMatrixRotationX(moho::VMatrix4* const outMatrix, const float angle)
  {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    LoadIdentity(*outMatrix);
    At(*outMatrix, 1, 1) = c;
    At(*outMatrix, 1, 2) = s;
    At(*outMatrix, 2, 1) = -s;
    At(*outMatrix, 2, 2) = c;
    return outMatrix;
  }

  moho::VMatrix4* D3DXMatrixRotationY(moho::VMatrix4* const outMatrix, const float angle)
  {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    LoadIdentity(*outMatrix);
    At(*outMatrix, 0, 0) = c;
    At(*outMatrix, 0, 2) = -s;
    At(*outMatrix, 2, 0) = s;
    At(*outMatrix, 2, 2) = c;
    return outMatrix;
  }

  moho::VMatrix4* D3DXMatrixRotationZ(moho::VMatrix4* const outMatrix, const float angle)
  {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    LoadIdentity(*outMatrix);
    At(*outMatrix, 0, 0) = c;
    At(*outMatrix, 0, 1) = s;
    At(*outMatrix, 1, 0) = -s;
    At(*outMatrix, 1, 1) = c;
    return outMatrix;
  }

  // D3DX normalizes the axis first.
  moho::VMatrix4* D3DXMatrixRotationAxis(moho::VMatrix4* const outMatrix, const Wm3::Vector3f* const axis, const float angle)
  {
    const Vec3 v = Normalize(Vec3{axis->x, axis->y, axis->z});
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float t = 1.0f - c;
    LoadIdentity(*outMatrix);
    At(*outMatrix, 0, 0) = t * v.x * v.x + c;
    At(*outMatrix, 0, 1) = t * v.x * v.y + s * v.z;
    At(*outMatrix, 0, 2) = t * v.x * v.z - s * v.y;
    At(*outMatrix, 1, 0) = t * v.x * v.y - s * v.z;
    At(*outMatrix, 1, 1) = t * v.y * v.y + c;
    At(*outMatrix, 1, 2) = t * v.y * v.z + s * v.x;
    At(*outMatrix, 2, 0) = t * v.x * v.z + s * v.y;
    At(*outMatrix, 2, 1) = t * v.y * v.z - s * v.x;
    At(*outMatrix, 2, 2) = t * v.z * v.z + c;
    return outMatrix;
  }

  // The quaternion is a D3DXQUATERNION in memory: x, y, z, w (moho::VMatrix4::LoadRotate
  // stages a Wm3 quaternion in that order before the call).
  moho::VMatrix4* D3DXMatrixRotationQuaternion(moho::VMatrix4* const outMatrix, const Wm3::Quaternionf* const rotation)
  {
    const float* const q = reinterpret_cast<const float*>(rotation);
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    LoadIdentity(*outMatrix);
    At(*outMatrix, 0, 0) = 1.0f - 2.0f * (y * y + z * z);
    At(*outMatrix, 0, 1) = 2.0f * (x * y + z * w);
    At(*outMatrix, 0, 2) = 2.0f * (x * z - y * w);
    At(*outMatrix, 1, 0) = 2.0f * (x * y - z * w);
    At(*outMatrix, 1, 1) = 1.0f - 2.0f * (x * x + z * z);
    At(*outMatrix, 1, 2) = 2.0f * (y * z + x * w);
    At(*outMatrix, 2, 0) = 2.0f * (x * z + y * w);
    At(*outMatrix, 2, 1) = 2.0f * (y * z - x * w);
    At(*outMatrix, 2, 2) = 1.0f - 2.0f * (x * x + y * y);
    return outMatrix;
  }

  moho::VMatrix4* D3DXMatrixLookAtRH(
    moho::VMatrix4* const outMatrix, const Wm3::Vector3f* const eye, const Wm3::Vector3f* const target, const Wm3::Vector3f* const up
  )
  {
    const Vec3 eyePoint{eye->x, eye->y, eye->z};
    const Vec3 zAxis = Normalize(Vec3{eye->x - target->x, eye->y - target->y, eye->z - target->z});
    const Vec3 xAxis = Normalize(Cross(Vec3{up->x, up->y, up->z}, zAxis));
    const Vec3 yAxis = Cross(zAxis, xAxis);
    LoadIdentity(*outMatrix);
    At(*outMatrix, 0, 0) = xAxis.x;
    At(*outMatrix, 0, 1) = yAxis.x;
    At(*outMatrix, 0, 2) = zAxis.x;
    At(*outMatrix, 1, 0) = xAxis.y;
    At(*outMatrix, 1, 1) = yAxis.y;
    At(*outMatrix, 1, 2) = zAxis.y;
    At(*outMatrix, 2, 0) = xAxis.z;
    At(*outMatrix, 2, 1) = yAxis.z;
    At(*outMatrix, 2, 2) = zAxis.z;
    At(*outMatrix, 3, 0) = -Dot(xAxis, eyePoint);
    At(*outMatrix, 3, 1) = -Dot(yAxis, eyePoint);
    At(*outMatrix, 3, 2) = -Dot(zAxis, eyePoint);
    return outMatrix;
  }
} // extern "C"
