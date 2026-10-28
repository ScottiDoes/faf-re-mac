// POSIX stand-in for gpg/gal/backends/common/WindowsCursor.cpp (excluded in
// macos/exclusions.d/render.cmake): the colour cursor the Vulkan backend shows in place of a
// hardware cursor (the engine takes it for every API but Direct3D 9: moho::d3d_WindowsCursor).
//
// Same flow as the Windows file: the 32x32 cursor texture is read through a lock (B,G,R,A,
// or R,G,B,A swapped to it), made into a cursor with its hotspot, put on screen with the
// Window area's SetCursor (which shows it through SDL while the window system is up) and
// shown or hidden with ShowCursor's display count. The cursor is the window system's colour
// cursor (faf_posix::shell::CreateColorCursor, SDL_CreateColorCursor) at one display point
// per drawable pixel's worth of density, so on Retina it keeps its size relative to the
// pixel-sized UI. Headless there is no window system and no cursor is made: the calls are
// logged no-ops, as before. A cursor that cannot be made is logged rather than thrown (the
// Windows file throws "attempt to use uninitialized cursor" from inside the window procedure).

#include "gpg/gal/backends/common/WindowsCursor.h"

#include <cstdint>

#include "gpg/gal/CursorContext.hpp"
#include "gpg/gal/D3D9Utils.h"
#include "gpg/gal/Texture.hpp"
#include "platform/posix/shell/ShellInternal.h"

namespace gpg::gal
{
  namespace
  {
    constexpr int kCursorSize = 32;

    // The cursor from the texture's top-left 32x32 texels, rows top-down, 0xAARRGGBB.
    HCURSOR BuildCursor(const int hotspotX, const int hotspotY, Texture& texture, const bool rgbaSource)
    {
      std::uint32_t pixels[kCursorSize * kCursorSize] = {};
      const RECT wholeSurface{};
      const TextureLockRect lock = texture.Lock(0, wholeSurface, static_cast<int>(MohoD3DLockFlags::ReadOnly));
      if (lock.bits == nullptr) {
        return nullptr;
      }
      const auto* const sourceBytes = static_cast<const std::uint8_t*>(lock.bits);
      for (int y = 0; y < kCursorSize; ++y) {
        const auto* const sourceRow = reinterpret_cast<const std::uint32_t*>(sourceBytes + static_cast<std::size_t>(lock.pitch) * y);
        for (int x = 0; x < kCursorSize; ++x) {
          const std::uint32_t pixel = sourceRow[x];
          pixels[y * kCursorSize + x] =
            rgbaSource ? ((pixel & 0xFF00FF00U) | ((pixel & 0x000000FFU) << 16U) | ((pixel & 0x00FF0000U) >> 16U)) : pixel;
        }
      }
      static_cast<void>(texture.Unlock(lock));
      const float density = faf_posix::shell::PixelDensity(::GetActiveWindow());
      return faf_posix::shell::CreateColorCursor(pixels, kCursorSize, hotspotX, hotspotY, density);
    }
  } // namespace

  WindowsCursor::~WindowsCursor()
  {
    Destroy();
  }

  void WindowsCursor::Destroy() noexcept
  {
    if (mCursorIcon != nullptr) {
      ::SetCursor(mPreviousCursor);
      ::DestroyIcon(mCursorIcon);
    }
    mPreviousCursor = nullptr;
    mCursorIcon = nullptr;
  }

  HCURSOR WindowsCursor::SetCursor(const CursorContext& context)
  {
    if (!faf_posix::shell::IsActive()) {
      FAF_POSIX_STUB("gal::WindowsCursor::SetCursor: no cursor without the window system");
      return nullptr;
    }
    HICON const icon = (context.texture_ != nullptr)
      ? BuildCursor(context.hotspotX_, context.hotspotY_, *context.texture_, mIsRgba != nullptr && mIsRgba(*context.texture_))
      : nullptr;
    HICON const previousIcon = mCursorIcon;
    mCursorIcon = icon;
    HCURSOR const previous = ::SetCursor(mCursorIcon);
    if (previousIcon == nullptr) {
      mPreviousCursor = previous;
    } else {
      ::DestroyIcon(previousIcon);
    }
    return previous;
  }

  HCURSOR WindowsCursor::InitCursor()
  {
    if (mCursorIcon == nullptr) {
      FAF_POSIX_STUB("gal::WindowsCursor::InitCursor: no cursor (headless, or the cursor could not be made)");
      return nullptr;
    }
    return ::SetCursor(mCursorIcon);
  }

  int WindowsCursor::ShowCursor(const bool show)
  {
    if (mCursorIcon == nullptr) {
      FAF_POSIX_STUB("gal::WindowsCursor::ShowCursor: no cursor (headless, or the cursor could not be made)");
      // The display count ShowCursor settles on: 0 when shown, -1 when hidden.
      return show ? 0 : -1;
    }
    int count = 0;
    if (show) {
      do {
        count = ::ShowCursor(TRUE);
      } while (count < 0);
    } else {
      do {
        count = ::ShowCursor(FALSE);
      } while (count >= 0);
    }
    return count;
  }
} // namespace gpg::gal
