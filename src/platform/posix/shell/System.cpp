// Displays (monitors, system metrics, the work area) and the clipboard while the shell is
// active. Monitors are SDL's displays, primary first, their rectangles in screen pixels
// (screen points times the primary display's pixel density). The clipboard is the system
// clipboard's text, handed out as Windows does: CF_UNICODETEXT / CF_TEXT in GlobalAlloc
// memory owned by the clipboard (wide text is wchar_t, as in-memory wide text is on POSIX).

#include "platform/posix/shell/ShellState.h"
#include "platform/posix/shell/Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace faf_posix::shell
{
  namespace detail
  {
    namespace
    {
      struct Monitor
      {
        SDL_DisplayID id = 0;
        RECT bounds{};
        RECT work{};
      };

      std::vector<Monitor>& Monitors() noexcept
      {
        static auto* const sMonitors = new std::vector<Monitor>();
        return *sMonitors;
      }

      constexpr std::uintptr_t kFirstMonitor = 0x00010001u;

      RECT ToPixels(const SDL_Rect& rect, float scale) noexcept
      {
        return RECT{static_cast<LONG>(std::lround(rect.x * scale)), static_cast<LONG>(std::lround(rect.y * scale)),
                    static_cast<LONG>(std::lround((rect.x + rect.w) * scale)),
                    static_cast<LONG>(std::lround((rect.y + rect.h) * scale))};
      }

      int MonitorIndex(HMONITOR monitor) noexcept
      {
        const auto value = reinterpret_cast<std::uintptr_t>(monitor);
        if (value < kFirstMonitor) {
          return -1;
        }
        const auto index = static_cast<std::size_t>(value - kFirstMonitor);
        return index < Monitors().size() ? static_cast<int>(index) : -1;
      }

      HMONITOR MonitorHandle(std::size_t index) noexcept
      {
        return reinterpret_cast<HMONITOR>(kFirstMonitor + index);
      }

      RECT VirtualScreen() noexcept
      {
        const auto& monitors = Monitors();
        if (monitors.empty()) {
          return RECT{0, 0, 1920, 1080};
        }
        RECT all = monitors.front().bounds;
        for (const Monitor& m : monitors) {
          all.left = std::min(all.left, m.bounds.left);
          all.top = std::min(all.top, m.bounds.top);
          all.right = std::max(all.right, m.bounds.right);
          all.bottom = std::max(all.bottom, m.bounds.bottom);
        }
        return all;
      }

      LONG Distance(const RECT& rect, POINT point) noexcept
      {
        const LONG dx = point.x < rect.left ? rect.left - point.x : (point.x >= rect.right ? point.x - rect.right + 1 : 0);
        const LONG dy = point.y < rect.top ? rect.top - point.y : (point.y >= rect.bottom ? point.y - rect.bottom + 1 : 0);
        return dx * dx + dy * dy;
      }

      // The clipboard's state between OpenClipboard and CloseClipboard.
      struct Clipboard
      {
        bool open = false;
        HGLOBAL lastData = nullptr; // what GetClipboardData handed out (the clipboard owns it)
      };

      Clipboard& Board() noexcept
      {
        static Clipboard sBoard;
        return sBoard;
      }
    } // namespace

    void RefreshDisplays() noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      int count = 0;
      SDL_DisplayID* const ids = SDL_GetDisplays(&count);
      const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
      // Screen coordinates in the main display's pixels (Panel.h); what was learned about
      // the displays is relearned, since they changed.
      ForgetDisplays();
      const float scale = DisplayScale(primary);
      s.screenScale = scale;
      auto& monitors = Monitors();
      monitors.clear();
      for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; ids != nullptr && i < count; ++i) {
          if ((ids[i] == primary) != (pass == 0)) {
            continue;
          }
          Monitor monitor{};
          monitor.id = ids[i];
          SDL_Rect bounds{};
          SDL_Rect usable{};
          if (!SDL_GetDisplayBounds(ids[i], &bounds)) {
            continue;
          }
          if (!SDL_GetDisplayUsableBounds(ids[i], &usable)) {
            usable = bounds;
          }
          monitor.bounds = ToPixels(bounds, scale);
          monitor.work = ToPixels(usable, scale);
          monitors.push_back(monitor);
        }
      }
      SDL_free(ids);
    }

    void ReleaseClipboard() noexcept
    {
      Clipboard& board = Board();
      if (board.lastData != nullptr) {
        (void)::GlobalFree(board.lastData);
        board.lastData = nullptr;
      }
      board.open = false;
    }
  } // namespace detail

  namespace win32
  {
    using namespace detail;

    int GetSystemMetrics(const int nIndex, BOOL* const handled) noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      const auto& monitors = Monitors();
      const RECT primary = monitors.empty() ? RECT{0, 0, 1920, 1080} : monitors.front().bounds;
      const RECT work = monitors.empty() ? primary : monitors.front().work;
      const RECT all = VirtualScreen();
      const int caption = static_cast<int>(std::lround(28.0f * s.screenScale));
      if (handled != nullptr) {
        *handled = TRUE;
      }
      switch (nIndex) {
        case SM_CXSCREEN: return primary.right - primary.left;
        case SM_CYSCREEN: return primary.bottom - primary.top;
        case SM_XVIRTUALSCREEN: return all.left;
        case SM_YVIRTUALSCREEN: return all.top;
        case SM_CXVIRTUALSCREEN: return all.right - all.left;
        case SM_CYVIRTUALSCREEN: return all.bottom - all.top;
        case SM_CMONITORS: return static_cast<int>(std::max<std::size_t>(1, monitors.size()));
        case SM_CXFULLSCREEN: return work.right - work.left;
        case SM_CYFULLSCREEN: return work.bottom - work.top - caption;
        case SM_CXMAXIMIZED: return work.right - work.left;
        case SM_CYMAXIMIZED: return work.bottom - work.top;
        case SM_CYCAPTION: return caption;
        case SM_CXFRAME:
        case SM_CYFRAME:
        case SM_CXBORDER:
        case SM_CYBORDER:
        case SM_CXDLGFRAME:
        case SM_CYDLGFRAME:
          return 0; // AppKit windows have no side frame
        case SM_CXCURSOR:
        case SM_CYCURSOR:
          return static_cast<int>(std::lround(16.0f * s.screenScale));
        case SM_CXDOUBLECLK:
        case SM_CYDOUBLECLK:
        case SM_CXDRAG:
        case SM_CYDRAG:
          return static_cast<int>(std::lround(4.0f * s.screenScale));
        case SM_MOUSEPRESENT:
          return 1;
        case SM_CMOUSEBUTTONS:
          return 5;
        default:
          if (handled != nullptr) {
            *handled = FALSE;
          }
          return 0;
      }
    }

    BOOL SystemParametersInfoW(const UINT uiAction, const UINT uiParam, const PVOID pvParam, const UINT fWinIni,
                               BOOL* const handled) noexcept
    {
      (void)uiParam;
      (void)fWinIni;
      if (handled != nullptr) {
        *handled = FALSE;
      }
      if (uiAction != SPI_GETWORKAREA) {
        return FALSE;
      }
      if (handled != nullptr) {
        *handled = TRUE;
      }
      if (pvParam == nullptr) {
        return FALSE;
      }
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      const auto& monitors = Monitors();
      *static_cast<RECT*>(pvParam) = monitors.empty() ? RECT{0, 0, 1920, 1080} : monitors.front().work;
      return TRUE;
    }

    HMONITOR MonitorFromPoint(const POINT pt, const DWORD dwFlags) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      const auto& monitors = Monitors();
      std::size_t nearest = 0;
      LONG best = -1;
      for (std::size_t i = 0; i < monitors.size(); ++i) {
        const LONG d = Distance(monitors[i].bounds, pt);
        if (d == 0) {
          return MonitorHandle(i);
        }
        if (best < 0 || d < best) {
          best = d;
          nearest = i;
        }
      }
      if (monitors.empty() || dwFlags == MONITOR_DEFAULTTONULL) {
        return monitors.empty() && dwFlags != MONITOR_DEFAULTTONULL ? MonitorHandle(0) : nullptr;
      }
      return MonitorHandle(dwFlags == MONITOR_DEFAULTTONEAREST ? nearest : 0);
    }

    HMONITOR MonitorFromRect(const LPCRECT lprc, const DWORD dwFlags) noexcept
    {
      if (lprc == nullptr) {
        return dwFlags == MONITOR_DEFAULTTONULL ? nullptr : MonitorHandle(0);
      }
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        const auto& monitors = Monitors();
        std::size_t bestIndex = 0;
        long long bestArea = 0;
        for (std::size_t i = 0; i < monitors.size(); ++i) {
          const RECT& m = monitors[i].bounds;
          const long long w = std::min(m.right, lprc->right) - std::max(m.left, lprc->left);
          const long long h = std::min(m.bottom, lprc->bottom) - std::max(m.top, lprc->top);
          if (w > 0 && h > 0 && w * h > bestArea) {
            bestArea = w * h;
            bestIndex = i;
          }
        }
        if (bestArea > 0) {
          return MonitorHandle(bestIndex);
        }
      }
      const POINT center{(lprc->left + lprc->right) / 2, (lprc->top + lprc->bottom) / 2};
      return win32::MonitorFromPoint(center, dwFlags);
    }

    HMONITOR MonitorFromWindow(const HWND hwnd, const DWORD dwFlags) noexcept
    {
      RECT rect{};
      if (win32::GetWindowRect(hwnd, &rect) == FALSE) {
        return dwFlags == MONITOR_DEFAULTTONULL ? nullptr : MonitorHandle(0);
      }
      return win32::MonitorFromRect(&rect, dwFlags);
    }

    BOOL GetMonitorInfoW(const HMONITOR hMonitor, const LPMONITORINFO lpmi) noexcept
    {
      if (lpmi == nullptr || lpmi->cbSize < sizeof(MONITORINFO)) {
        return FALSE;
      }
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      const int index = MonitorIndex(hMonitor);
      const auto& monitors = Monitors();
      RECT bounds{0, 0, 1920, 1080};
      RECT work = bounds;
      if (index >= 0) {
        bounds = monitors[static_cast<std::size_t>(index)].bounds;
        work = monitors[static_cast<std::size_t>(index)].work;
      } else if (!(monitors.empty() && hMonitor == MonitorHandle(0))) {
        return FALSE;
      }
      lpmi->rcMonitor = bounds;
      lpmi->rcWork = work;
      lpmi->dwFlags = index <= 0 ? MONITORINFOF_PRIMARY : 0;
      if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) {
        auto* const extended = reinterpret_cast<LPMONITORINFOEXW>(lpmi);
        std::swprintf(extended->szDevice, CCHDEVICENAME, L"\\\\.\\DISPLAY%d", std::max(0, index) + 1);
      }
      return TRUE;
    }

    // --- Clipboard ---------------------------------------------------------------------------
    BOOL OpenClipboard(const HWND hWndNewOwner) noexcept
    {
      (void)hWndNewOwner;
      Clipboard& board = Board();
      if (board.open) {
        ::SetLastError(5L /* ERROR_ACCESS_DENIED */);
        return FALSE;
      }
      board.open = true;
      return TRUE;
    }

    BOOL CloseClipboard() noexcept
    {
      Clipboard& board = Board();
      if (!board.open) {
        return FALSE;
      }
      board.open = false;
      return TRUE;
    }

    BOOL EmptyClipboard() noexcept
    {
      Clipboard& board = Board();
      if (!board.open) {
        return FALSE;
      }
      (void)SDL_ClearClipboardData();
      return TRUE;
    }

    HANDLE SetClipboardData(const UINT uFormat, const HANDLE hMem) noexcept
    {
      Clipboard& board = Board();
      if (!board.open || hMem == nullptr) {
        return nullptr;
      }
      const void* const data = ::GlobalLock(hMem);
      if (data == nullptr) {
        return nullptr;
      }
      std::string text;
      if (uFormat == CF_UNICODETEXT) {
        text = Utf8FromWide(static_cast<const wchar_t*>(data));
      } else if (uFormat == CF_TEXT || uFormat == CF_OEMTEXT) {
        text = static_cast<const char*>(data);
      } else {
        (void)::GlobalUnlock(hMem);
        FAF_POSIX_STUB("SetClipboardData: only text formats reach the system clipboard on POSIX");
        return nullptr;
      }
      (void)::GlobalUnlock(hMem);
      if (!SDL_SetClipboardText(text.c_str())) {
        return nullptr;
      }
      // The clipboard owns the block from here; its text is in the system clipboard now.
      (void)::GlobalFree(hMem);
      return hMem;
    }

    HANDLE GetClipboardData(const UINT uFormat) noexcept
    {
      Clipboard& board = Board();
      if (!board.open || (uFormat != CF_UNICODETEXT && uFormat != CF_TEXT && uFormat != CF_OEMTEXT)) {
        return nullptr;
      }
      char* const utf8 = SDL_GetClipboardText();
      if (utf8 == nullptr) {
        return nullptr;
      }
      HGLOBAL block = nullptr;
      if (uFormat == CF_UNICODETEXT) {
        const std::wstring wide = WideFromUtf8(utf8);
        block = ::GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (block != nullptr) {
          auto* const out = static_cast<wchar_t*>(::GlobalLock(block));
          std::wmemcpy(out, wide.c_str(), wide.size() + 1);
          (void)::GlobalUnlock(block);
        }
      } else {
        const std::size_t length = std::strlen(utf8);
        block = ::GlobalAlloc(GMEM_MOVEABLE, length + 1);
        if (block != nullptr) {
          std::memcpy(::GlobalLock(block), utf8, length + 1);
          (void)::GlobalUnlock(block);
        }
      }
      SDL_free(utf8);
      if (board.lastData != nullptr) {
        (void)::GlobalFree(board.lastData);
      }
      board.lastData = block;
      return block;
    }

    BOOL IsClipboardFormatAvailable(const UINT format) noexcept
    {
      if (format != CF_UNICODETEXT && format != CF_TEXT && format != CF_OEMTEXT) {
        return FALSE;
      }
      return SDL_HasClipboardText() ? TRUE : FALSE;
    }
  } // namespace win32
} // namespace faf_posix::shell
