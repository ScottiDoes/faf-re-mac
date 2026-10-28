// Test doubles for fonts_test (see test_doubles.h): the few engine and compat symbols
// CD3DFont.cpp and the Gdi area reach outside the files the test links.

#include "macos/tests/fonts/test_doubles.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gpg/core/containers/String.h"
#include "gpg/core/utils/Logging.h"
#include "moho/console/CConCommand.h"
#include "moho/math/Vector4f.h"
#include "moho/render/d3d/CD3DFontTypes.h"
#include "moho/render/d3d/CD3DPrimBatcher.h"
#include "moho/render/textures/CD3DBatchTexture.h"

namespace fonts_test
{
  std::map<const void*, Dxt5Texture>& Textures()
  {
    static std::map<const void*, Dxt5Texture> textures;
    return textures;
  }

  std::vector<Quad>& Quads()
  {
    static std::vector<Quad> quads;
    return quads;
  }

  std::vector<std::string>& Warnings()
  {
    static std::vector<std::string> warnings;
    return warnings;
  }

  std::string& Context()
  {
    static std::string context;
    return context;
  }
} // namespace fonts_test

// --- Compat (Errors, Window) ---------------------------------------------------------------------
namespace
{
  thread_local DWORD tLastError = 0;
}

void SetLastError(const DWORD code) noexcept
{
  tLastError = code;
}

DWORD GetLastError() noexcept
{
  return tLastError;
}

BOOL IsWindow(HWND) noexcept
{
  return FALSE;
}

extern "C" void faf_posixReportStub(const char* const what, const char* const file, const int line) noexcept
{
  std::fprintf(stderr, "[faf-posix] stub: %s (%s:%d)\n", what, file, line);
}

// --- gpg -----------------------------------------------------------------------------------------
void gpg::Warnf(const char* const fmt, ...)
{
  char text[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  fonts_test::Warnings().push_back(fonts_test::Context() + ": " + text);
}

// UTF-8 to code points (the POSIX STR_DecodeUtf8Char's results for valid text).
const char* gpg::STR_DecodeUtf8Char(const char* const str, wchar_t& dest)
{
  const auto* p = reinterpret_cast<const unsigned char*>(str);
  if (p[0] < 0x80U) {
    dest = static_cast<wchar_t>(p[0]);
    return str + 1;
  }
  int extra = 0;
  std::uint32_t code = 0;
  if ((p[0] & 0xE0U) == 0xC0U) {
    extra = 1;
    code = p[0] & 0x1FU;
  } else if ((p[0] & 0xF0U) == 0xE0U) {
    extra = 2;
    code = p[0] & 0x0FU;
  } else if ((p[0] & 0xF8U) == 0xF0U) {
    extra = 3;
    code = p[0] & 0x07U;
  } else {
    dest = static_cast<wchar_t>(0xFF00U | p[0]);
    return str + 1;
  }
  for (int i = 1; i <= extra; ++i) {
    if ((p[i] & 0xC0U) != 0x80U) {
      dest = 0;
      return str + i;
    }
    code = (code << 6) | (p[i] & 0x3FU);
  }
  dest = static_cast<wchar_t>(code);
  return str + 1 + extra;
}

// --- moho ----------------------------------------------------------------------------------------
moho::CConCommand::CConCommand(const char* const name, const char* const description) noexcept
  : mName(name)
  , mDescription(description)
{
}

moho::CConCommand::~CConCommand() = default;

template <>
void moho::TConVar<bool>::Handle(const msvc8::vector<msvc8::string>&)
{
}

moho::Vector4f::Vector4f(const float xValue, const float yValue, const float zValue, const float wValue) noexcept
  : x(xValue)
  , y(yValue)
  , z(zValue)
  , w(wValue)
{
}

moho::Vector4f& moho::Vector4f::operator=(const Vector4f& rhs) noexcept
{
  x = rhs.x;
  y = rhs.y;
  z = rhs.z;
  w = rhs.w;
  return *this;
}

// The glyph upload: records the DXT5 blocks; the handle is an opaque token never dereferenced.
boost::shared_ptr<moho::CD3DBatchTexture> moho::CD3DBatchTexture::FromDXT5(
  const std::uint32_t width, const std::uint32_t height, const void* const dxt5Blocks, const std::uint32_t sourcePitchBytes
)
{
  auto* const token = static_cast<moho::CD3DBatchTexture*>(std::malloc(16));
  fonts_test::Dxt5Texture texture;
  texture.width = width;
  texture.height = height;
  const std::size_t words = static_cast<std::size_t>(width / 4U) * (height / 4U) * 2U;
  texture.blocks.resize(words);
  // Rows of blocks are `sourcePitchBytes` apart (16 bytes per block).
  const auto* const source = static_cast<const std::uint8_t*>(dxt5Blocks);
  for (std::uint32_t row = 0; row < height / 4U; ++row) {
    std::memcpy(
      texture.blocks.data() + static_cast<std::size_t>(row) * (width / 4U) * 2U, source + static_cast<std::size_t>(row) * sourcePitchBytes,
      static_cast<std::size_t>(width / 4U) * 16U
    );
  }
  fonts_test::Textures()[token] = std::move(texture);
  return boost::shared_ptr<moho::CD3DBatchTexture>(token, [](moho::CD3DBatchTexture*) {});
}

// The quad: the corners exactly as the real DrawChar builds them (origin + xAxis * u + yAxis * v).
void moho::CD3DPrimBatcher::DrawChar(
  const Vector3f& xAxis, const Vector3f& yAxis, const CD3DFontCharInfo& charInfo, const Vector3f& origin, const std::uint32_t color
)
{
  fonts_test::Quad quad;
  quad.texture = charInfo.mTex.get();
  quad.left = origin.x + xAxis.x * charInfo.mV2 + yAxis.x * charInfo.mV5;
  quad.top = origin.y + xAxis.y * charInfo.mV2 + yAxis.y * charInfo.mV5;
  quad.right = origin.x + xAxis.x * charInfo.mV4 + yAxis.x * charInfo.mV3;
  quad.bottom = origin.y + xAxis.y * charInfo.mV4 + yAxis.y * charInfo.mV3;
  quad.color = color;
  fonts_test::Quads().push_back(quad);
}
