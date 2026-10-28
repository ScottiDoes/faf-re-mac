// SDL events -> Win32 messages, the key state, the mouse and the cursor.
//
// Keys: SDL's scancode gives the PC scan code (lParam bits 16..24) and the virtual key of
// keys whose meaning does not depend on the layout; letters follow the layout (SDL's
// keycode), as Windows' virtual keys do. Ctrl is VK_CONTROL, Option is VK_MENU (Alt: the
// SYSKEY messages and lParam bit 29), Command is VK_LWIN / VK_RWIN. Text arrives from SDL's
// text input as WM_CHAR, one message per UTF-16 unit; TranslateMessage adds the control
// characters (Queue.cpp).
//
// Mouse: messages go to the window under the cursor (child windows included) or to the
// capture window, in its client pixels; WM_MOUSEWHEEL goes to the focus window with screen
// coordinates and WHEEL_DELTA units, in the physical direction (macOS's natural scrolling
// is undone, so the wheel zooms as on Windows). Double clicks follow AppKit's click count
// for windows whose class has CS_DBLCLKS.

#include "platform/posix/shell/ShellState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace faf_posix::shell
{
  namespace detail
  {
    namespace
    {
      struct KeyCode
      {
        BYTE vk = 0;
        BYTE scan = 0;
        bool extended = false;
      };

      KeyCode FromScancode(const SDL_Scancode code) noexcept
      {
        if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
          static constexpr BYTE kLetterScans[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                    0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                    0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
          const int index = code - SDL_SCANCODE_A;
          return {static_cast<BYTE>('A' + index), kLetterScans[index], false};
        }
        if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
          const int index = code - SDL_SCANCODE_1;
          return {static_cast<BYTE>('1' + index), static_cast<BYTE>(0x02 + index), false};
        }
        if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F10) {
          const int index = code - SDL_SCANCODE_F1;
          return {static_cast<BYTE>(VK_F1 + index), static_cast<BYTE>(0x3B + index), false};
        }
        if (code >= SDL_SCANCODE_F13 && code <= SDL_SCANCODE_F23) {
          const int index = code - SDL_SCANCODE_F13;
          return {static_cast<BYTE>(VK_F13 + index), static_cast<BYTE>(0x64 + index), false};
        }
        switch (code) {
          case SDL_SCANCODE_0: return {'0', 0x0B, false};
          case SDL_SCANCODE_RETURN: return {VK_RETURN, 0x1C, false};
          case SDL_SCANCODE_ESCAPE: return {VK_ESCAPE, 0x01, false};
          case SDL_SCANCODE_BACKSPACE: return {VK_BACK, 0x0E, false};
          case SDL_SCANCODE_TAB: return {VK_TAB, 0x0F, false};
          case SDL_SCANCODE_SPACE: return {VK_SPACE, 0x39, false};
          case SDL_SCANCODE_MINUS: return {VK_OEM_MINUS, 0x0C, false};
          case SDL_SCANCODE_EQUALS: return {VK_OEM_PLUS, 0x0D, false};
          case SDL_SCANCODE_LEFTBRACKET: return {VK_OEM_4, 0x1A, false};
          case SDL_SCANCODE_RIGHTBRACKET: return {VK_OEM_6, 0x1B, false};
          case SDL_SCANCODE_BACKSLASH: return {VK_OEM_5, 0x2B, false};
          case SDL_SCANCODE_NONUSHASH: return {VK_OEM_5, 0x2B, false};
          case SDL_SCANCODE_SEMICOLON: return {VK_OEM_1, 0x27, false};
          case SDL_SCANCODE_APOSTROPHE: return {VK_OEM_7, 0x28, false};
          case SDL_SCANCODE_GRAVE: return {VK_OEM_3, 0x29, false};
          case SDL_SCANCODE_COMMA: return {VK_OEM_COMMA, 0x33, false};
          case SDL_SCANCODE_PERIOD: return {VK_OEM_PERIOD, 0x34, false};
          case SDL_SCANCODE_SLASH: return {VK_OEM_2, 0x35, false};
          case SDL_SCANCODE_NONUSBACKSLASH: return {VK_OEM_102, 0x56, false};
          case SDL_SCANCODE_CAPSLOCK: return {VK_CAPITAL, 0x3A, false};
          case SDL_SCANCODE_F11: return {VK_F11, 0x57, false};
          case SDL_SCANCODE_F12: return {VK_F12, 0x58, false};
          case SDL_SCANCODE_F24: return {VK_F24, 0x76, false};
          case SDL_SCANCODE_PRINTSCREEN: return {VK_SNAPSHOT, 0x37, true};
          case SDL_SCANCODE_SCROLLLOCK: return {VK_SCROLL, 0x46, false};
          case SDL_SCANCODE_PAUSE: return {VK_PAUSE, 0x45, false};
          case SDL_SCANCODE_INSERT: return {VK_INSERT, 0x52, true};
          case SDL_SCANCODE_HOME: return {VK_HOME, 0x47, true};
          case SDL_SCANCODE_PAGEUP: return {VK_PRIOR, 0x49, true};
          case SDL_SCANCODE_DELETE: return {VK_DELETE, 0x53, true};
          case SDL_SCANCODE_END: return {VK_END, 0x4F, true};
          case SDL_SCANCODE_PAGEDOWN: return {VK_NEXT, 0x51, true};
          case SDL_SCANCODE_RIGHT: return {VK_RIGHT, 0x4D, true};
          case SDL_SCANCODE_LEFT: return {VK_LEFT, 0x4B, true};
          case SDL_SCANCODE_DOWN: return {VK_DOWN, 0x50, true};
          case SDL_SCANCODE_UP: return {VK_UP, 0x48, true};
          case SDL_SCANCODE_NUMLOCKCLEAR: return {VK_NUMLOCK, 0x45, true};
          case SDL_SCANCODE_KP_DIVIDE: return {VK_DIVIDE, 0x35, true};
          case SDL_SCANCODE_KP_MULTIPLY: return {VK_MULTIPLY, 0x37, false};
          case SDL_SCANCODE_KP_MINUS: return {VK_SUBTRACT, 0x4A, false};
          case SDL_SCANCODE_KP_PLUS: return {VK_ADD, 0x4E, false};
          case SDL_SCANCODE_KP_ENTER: return {VK_RETURN, 0x1C, true};
          case SDL_SCANCODE_KP_1: return {VK_NUMPAD1, 0x4F, false};
          case SDL_SCANCODE_KP_2: return {VK_NUMPAD2, 0x50, false};
          case SDL_SCANCODE_KP_3: return {VK_NUMPAD3, 0x51, false};
          case SDL_SCANCODE_KP_4: return {VK_NUMPAD4, 0x4B, false};
          case SDL_SCANCODE_KP_5: return {VK_NUMPAD5, 0x4C, false};
          case SDL_SCANCODE_KP_6: return {VK_NUMPAD6, 0x4D, false};
          case SDL_SCANCODE_KP_7: return {VK_NUMPAD7, 0x47, false};
          case SDL_SCANCODE_KP_8: return {VK_NUMPAD8, 0x48, false};
          case SDL_SCANCODE_KP_9: return {VK_NUMPAD9, 0x49, false};
          case SDL_SCANCODE_KP_0: return {VK_NUMPAD0, 0x52, false};
          case SDL_SCANCODE_KP_PERIOD: return {VK_DECIMAL, 0x53, false};
          case SDL_SCANCODE_KP_EQUALS: return {0x92 /* VK_OEM_NEC_EQUAL */, 0x59, false};
          case SDL_SCANCODE_APPLICATION: return {VK_APPS, 0x5D, true};
          case SDL_SCANCODE_LCTRL: return {VK_CONTROL, 0x1D, false};
          case SDL_SCANCODE_RCTRL: return {VK_CONTROL, 0x1D, true};
          case SDL_SCANCODE_LSHIFT: return {VK_SHIFT, 0x2A, false};
          case SDL_SCANCODE_RSHIFT: return {VK_SHIFT, 0x36, false};
          case SDL_SCANCODE_LALT: return {VK_MENU, 0x38, false};
          case SDL_SCANCODE_RALT: return {VK_MENU, 0x38, true};
          case SDL_SCANCODE_LGUI: return {VK_LWIN, 0x5B, true};
          case SDL_SCANCODE_RGUI: return {VK_RWIN, 0x5C, true};
          case SDL_SCANCODE_HELP: return {VK_HELP, 0x63, false};
          default: return {};
        }
      }

      // The left/right key a generic modifier message stands for.
      BYTE SpecificModifier(BYTE vk, BYTE scan, bool extended) noexcept
      {
        switch (vk) {
          case VK_SHIFT: return scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
          case VK_CONTROL: return extended ? VK_RCONTROL : VK_LCONTROL;
          case VK_MENU: return extended ? VK_RMENU : VK_LMENU;
          default: return 0;
        }
      }

      void SetKey(std::array<std::atomic<std::uint8_t>, 256>& keys, BYTE vk, bool down, bool toggle) noexcept
      {
        std::uint8_t value = keys[vk].load(std::memory_order_relaxed);
        if (toggle) {
          value ^= 0x01;
        }
        value = down ? static_cast<std::uint8_t>(value | 0x80) : static_cast<std::uint8_t>(value & ~0x80);
        keys[vk].store(value, std::memory_order_relaxed);
      }

      void SetModifierKeys(std::array<std::atomic<std::uint8_t>, 256>& keys, BYTE vk, BYTE specific, bool down, bool toggle) noexcept
      {
        SetKey(keys, specific, down, toggle);
        BYTE left = 0;
        BYTE right = 0;
        switch (vk) {
          case VK_SHIFT: left = VK_LSHIFT; right = VK_RSHIFT; break;
          case VK_CONTROL: left = VK_LCONTROL; right = VK_RCONTROL; break;
          default: left = VK_LMENU; right = VK_RMENU; break;
        }
        const bool any = ((keys[left].load(std::memory_order_relaxed) | keys[right].load(std::memory_order_relaxed)) & 0x80) != 0;
        SetKey(keys, vk, any, toggle);
      }

      bool AsyncDown(BYTE vk) noexcept
      {
        return (S().asyncKeys[vk].load(std::memory_order_relaxed) & 0x80) != 0;
      }

      WPARAM MouseKeyFlags() noexcept
      {
        WPARAM flags = S().mouseButtons;
        if (AsyncDown(VK_SHIFT)) {
          flags |= MK_SHIFT;
        }
        if (AsyncDown(VK_CONTROL)) {
          flags |= MK_CONTROL;
        }
        return flags;
      }

      HWND TopLevelForSdl(SDL_WindowID id) noexcept
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const record = FindBySdl(id);
        return record != nullptr ? record->handle : nullptr;
      }

      MSG MakeMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, POINT screen) noexcept
      {
        MSG out{};
        out.hwnd = window;
        out.message = message;
        out.wParam = wParam;
        out.lParam = lParam;
        out.time = Now();
        out.pt = screen;
        return out;
      }

      // The window a mouse event at a point of a top-level window's client area goes to, and
      // the point in that window's client pixels. False when it goes nowhere (outside our
      // windows without a capture).
      bool RouteMouse(HWND topLevel, float x, float y, HWND* target, POINT* local, POINT* screen) noexcept
      {
        State& s = S();
        HWND capture = nullptr;
        float density = 1.0f;
        int originX = 0;
        int originY = 0;
        int clientW = 0;
        int clientH = 0;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          capture = s.capture;
          WindowRecord* const top = Find(topLevel);
          if (top != nullptr) {
            density = DensityOf(top);
            originX = top->screenX;
            originY = top->screenY;
            clientW = top->clientWidth;
            clientH = top->clientHeight;
          }
        }
        if (topLevel != nullptr) {
          *screen = ScreenPixels(static_cast<float>(originX) + x, static_cast<float>(originY) + y);
        } else {
          float gx = 0.0f;
          float gy = 0.0f;
          GlobalMouse(&gx, &gy);
          *screen = ScreenPixels(gx, gy);
        }
        if (capture != nullptr) {
          POINT point = *screen;
          (void)win32::ScreenToClient(capture, &point);
          *target = capture;
          *local = point;
          return true;
        }
        if (topLevel == nullptr) {
          return false;
        }
        const POINT client{static_cast<LONG>(std::floor(x * density + 1e-3f)), static_cast<LONG>(std::floor(y * density + 1e-3f))};
        if (client.x < 0 || client.y < 0 || client.x >= clientW || client.y >= clientH) {
          return false;
        }
        *target = HitTest(topLevel, client, local);
        return *target != nullptr;
      }

      bool WantsDoubleClicks(HWND window) noexcept
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const record = Find(window);
        return record != nullptr && (record->classStyle & CS_DBLCLKS) != 0;
      }

      void TranslateKey(const SDL_KeyboardEvent& key) noexcept
      {
        State& s = S();
        KeyCode code = FromScancode(key.scancode);
        if (code.vk == 0) {
          return;
        }
        // Letters follow the keyboard layout, as Windows' virtual keys do.
        if (key.key >= 'a' && key.key <= 'z' && code.vk >= 'A' && code.vk <= 'Z') {
          code.vk = static_cast<BYTE>(key.key - 'a' + 'A');
        }
        const bool down = key.down;
        const bool wasDown = AsyncDown(code.vk);
        const BYTE specific = SpecificModifier(code.vk, code.scan, code.extended);
        if (specific != 0) {
          SetModifierKeys(s.asyncKeys, code.vk, specific, down, false);
        } else {
          SetKey(s.asyncKeys, code.vk, down, false);
        }
        if (code.vk == VK_CAPITAL) {
          // AppKit reports the lock's state, not presses.
          const bool locked = (SDL_GetModState() & SDL_KMOD_CAPS) != 0;
          for (auto* keys : {&s.asyncKeys, &s.syncKeys}) {
            std::uint8_t value = (*keys)[VK_CAPITAL].load(std::memory_order_relaxed);
            value = locked ? static_cast<std::uint8_t>(value | 0x01) : static_cast<std::uint8_t>(value & ~0x01);
            (*keys)[VK_CAPITAL].store(value, std::memory_order_relaxed);
          }
        }

        const SDL_Keymod mods = key.mod;
        const bool alt = (mods & SDL_KMOD_ALT) != 0;
        const bool ctrl = (mods & SDL_KMOD_CTRL) != 0;
        LPARAM lParam = 1 | (static_cast<LPARAM>(code.scan) << 16) | (code.extended ? (1 << 24) : 0);
        if (alt) {
          lParam |= 1 << 29;
        }
        if (!down || key.repeat || wasDown) {
          lParam |= 1 << 30;
        }
        if (!down) {
          lParam |= static_cast<LPARAM>(1u << 31);
        }
        const bool sys = (alt && !ctrl) || code.vk == VK_F10 || (!down && code.vk == VK_MENU && !ctrl);
        UINT message = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);
        if (down) {
          s.lastScanInfo = static_cast<std::uint32_t>(lParam & 0x01FF0000);
        }

        HWND target = nullptr;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          target = s.focus;
          if (target == nullptr && s.activeWindow != nullptr) {
            // No focus: the active window gets the keys as system keys.
            target = s.activeWindow;
            message = down ? WM_SYSKEYDOWN : WM_SYSKEYUP;
          }
        }
        if (target == nullptr) {
          return;
        }
        float gx = 0.0f;
        float gy = 0.0f;
        GlobalMouse(&gx, &gy);
        Post(MakeMessage(target, message, code.vk, lParam, ScreenPixels(gx, gy)));
      }

      void TranslateText(const SDL_TextInputEvent& text) noexcept
      {
        State& s = S();
        // Ctrl+key makes its control character in TranslateMessage.
        if (AsyncDown(VK_CONTROL) && !AsyncDown(VK_MENU)) {
          return;
        }
        HWND target = nullptr;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          target = s.focus != nullptr ? s.focus : s.activeWindow;
        }
        if (target == nullptr || text.text == nullptr) {
          return;
        }
        const LPARAM lParam = 1 | static_cast<LPARAM>(s.lastScanInfo);
        float gx = 0.0f;
        float gy = 0.0f;
        GlobalMouse(&gx, &gy);
        const POINT screen = ScreenPixels(gx, gy);
        for (wchar_t cp : WideFromUtf8(text.text)) {
          const auto value = static_cast<std::uint32_t>(cp);
          if (value < 0x20 || value == 0x7F) {
            continue;
          }
          if (value >= 0x10000) {
            const std::uint32_t v = value - 0x10000;
            Post(MakeMessage(target, WM_CHAR, 0xD800 + (v >> 10), lParam, screen));
            Post(MakeMessage(target, WM_CHAR, 0xDC00 + (v & 0x3FF), lParam, screen));
          } else {
            Post(MakeMessage(target, WM_CHAR, value, lParam, screen));
          }
        }
      }

      void TranslateMotion(const SDL_MouseMotionEvent& motion) noexcept
      {
        HWND target = nullptr;
        POINT local{};
        POINT screen{};
        if (!RouteMouse(TopLevelForSdl(motion.windowID), motion.x, motion.y, &target, &local, &screen)) {
          return;
        }
        Post(MakeMessage(target, WM_MOUSEMOVE, MouseKeyFlags(),
                         MAKELPARAM(static_cast<WORD>(local.x), static_cast<WORD>(local.y)), screen));
      }

      void TranslateButton(const SDL_MouseButtonEvent& button) noexcept
      {
        State& s = S();
        UINT downMessage = 0;
        UINT upMessage = 0;
        UINT dblMessage = 0;
        std::uint32_t flag = 0;
        BYTE vk = 0;
        WORD xbutton = 0;
        switch (button.button) {
          case SDL_BUTTON_LEFT: downMessage = WM_LBUTTONDOWN; upMessage = WM_LBUTTONUP; dblMessage = WM_LBUTTONDBLCLK; flag = MK_LBUTTON; vk = VK_LBUTTON; break;
          case SDL_BUTTON_RIGHT: downMessage = WM_RBUTTONDOWN; upMessage = WM_RBUTTONUP; dblMessage = WM_RBUTTONDBLCLK; flag = MK_RBUTTON; vk = VK_RBUTTON; break;
          case SDL_BUTTON_MIDDLE: downMessage = WM_MBUTTONDOWN; upMessage = WM_MBUTTONUP; dblMessage = WM_MBUTTONDBLCLK; flag = MK_MBUTTON; vk = VK_MBUTTON; break;
          case SDL_BUTTON_X1: downMessage = WM_XBUTTONDOWN; upMessage = WM_XBUTTONUP; dblMessage = WM_XBUTTONDBLCLK; flag = MK_XBUTTON1; vk = VK_XBUTTON1; xbutton = XBUTTON1; break;
          case SDL_BUTTON_X2: downMessage = WM_XBUTTONDOWN; upMessage = WM_XBUTTONUP; dblMessage = WM_XBUTTONDBLCLK; flag = MK_XBUTTON2; vk = VK_XBUTTON2; xbutton = XBUTTON2; break;
          default: return;
        }
        const bool down = button.down;
        if (down) {
          s.mouseButtons |= flag;
        } else {
          s.mouseButtons &= ~flag;
        }
        SetKey(s.asyncKeys, vk, down, false);

        HWND target = nullptr;
        POINT local{};
        POINT screen{};
        if (!RouteMouse(TopLevelForSdl(button.windowID), button.x, button.y, &target, &local, &screen)) {
          return;
        }
        UINT message = down ? downMessage : upMessage;
        if (down && button.clicks >= 2 && (button.clicks % 2) == 0 && WantsDoubleClicks(target)) {
          message = dblMessage;
        }
        const WPARAM wParam = MouseKeyFlags() | (static_cast<WPARAM>(xbutton) << 16);
        Post(MakeMessage(target, message, wParam, MAKELPARAM(static_cast<WORD>(local.x), static_cast<WORD>(local.y)), screen));
      }

      void TranslateWheel(const SDL_MouseWheelEvent& wheel) noexcept
      {
        State& s = S();
        const HWND topLevel = TopLevelForSdl(wheel.windowID);
        float amount = wheel.y;
        if (wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          amount = -amount;
        }
        int delta = 0;
        HWND target = nullptr;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          WindowRecord* const top = Find(topLevel);
          float* remainder = top != nullptr ? &top->wheelRemainder : nullptr;
          float total = amount * static_cast<float>(WHEEL_DELTA) + (remainder != nullptr ? *remainder : 0.0f);
          delta = static_cast<int>(total);
          if (remainder != nullptr) {
            *remainder = total - static_cast<float>(delta);
          }
          target = s.focus != nullptr ? s.focus : s.activeWindow;
        }
        if (delta == 0) {
          return;
        }
        HWND under = nullptr;
        POINT local{};
        POINT screen{};
        const bool routed = RouteMouse(topLevel, wheel.mouse_x, wheel.mouse_y, &under, &local, &screen);
        if (target == nullptr) {
          target = routed ? under : nullptr;
        }
        if (target == nullptr) {
          return;
        }
        delta = std::clamp(delta, -32768, 32767);
        const WPARAM wParam = MouseKeyFlags() | (static_cast<WPARAM>(static_cast<WORD>(static_cast<short>(delta))) << 16);
        Post(MakeMessage(target, WM_MOUSEWHEEL, wParam, MAKELPARAM(static_cast<WORD>(screen.x), static_cast<WORD>(screen.y)), screen));
      }

      void TranslateWindowEvent(const SDL_WindowEvent& window, Uint32 type) noexcept
      {
        State& s = S();
        const HWND topLevel = TopLevelForSdl(window.windowID);
        if (topLevel == nullptr) {
          return;
        }
        switch (type) {
          case SDL_EVENT_WINDOW_EXPOSED:
            (void)win32::InvalidateRect(topLevel, nullptr, FALSE);
            {
              std::lock_guard<std::recursive_mutex> guard(s.lock);
              std::vector<HWND> stack{topLevel};
              while (!stack.empty()) {
                WindowRecord* const record = Find(stack.back());
                stack.pop_back();
                if (record != nullptr) {
                  record->invalid = true;
                  stack.insert(stack.end(), record->children.begin(), record->children.end());
                }
              }
            }
            break;
          case SDL_EVENT_WINDOW_MOVED:
          case SDL_EVENT_WINDOW_RESIZED:
          case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
          case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            // Sized by the user, or its pixels changed with the display's scale: either way
            // the sizing sequence (WM_EXITSIZEMOVE is what makes the game's frame resize its
            // viewport and the device).
            SyncTopLevel(topLevel, true);
            break;
          case SDL_EVENT_WINDOW_MINIMIZED:
          case SDL_EVENT_WINDOW_MAXIMIZED:
          case SDL_EVENT_WINDOW_RESTORED:
          case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
          case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            SyncTopLevel(topLevel, false);
            break;
          case SDL_EVENT_WINDOW_MOUSE_ENTER: {
            std::lock_guard<std::recursive_mutex> guard(s.lock);
            s.mouseTopLevel = topLevel;
            break;
          }
          case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
            std::lock_guard<std::recursive_mutex> guard(s.lock);
            if (s.mouseTopLevel == topLevel) {
              s.mouseTopLevel = nullptr;
            }
            break;
          }
          case SDL_EVENT_WINDOW_FOCUS_GAINED:
            OnFocusGained(topLevel);
            break;
          case SDL_EVENT_WINDOW_FOCUS_LOST:
            OnFocusLost(topLevel);
            break;
          case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            // The close button: WM_SYSCOMMAND(SC_CLOSE), DefWindowProc then sends WM_CLOSE.
            MSG message = MakeMessage(topLevel, WM_SYSCOMMAND, SC_CLOSE, 0, s.lastMessagePos);
            Post(message);
            break;
          }
          default:
            break;
        }
      }

      void TranslateQuit() noexcept
      {
        // Cmd+Q (the application menu): close the active window as Alt+F4 does.
        State& s = S();
        HWND target = nullptr;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          target = s.activeWindow;
          if (target == nullptr) {
            for (HWND top : s.topLevels) {
              WindowRecord* const record = Find(top);
              if (record != nullptr && record->visible) {
                target = top;
                break;
              }
            }
          }
        }
        if (target != nullptr) {
          Post(MakeMessage(target, WM_SYSCOMMAND, SC_CLOSE, 0, s.lastMessagePos));
        }
      }

      // HCURSOR handles: system cursors are their IDC_ resource ids (LoadCursorW), colour
      // cursors are numbered from here.
      constexpr std::uintptr_t kFirstColorCursor = 0x00400000u;
      std::uintptr_t sNextColorCursor = kFirstColorCursor;
      std::unordered_map<std::uintptr_t, SDL_Cursor*>& Cursors() noexcept
      {
        static auto* const sCursors = new std::unordered_map<std::uintptr_t, SDL_Cursor*>();
        return *sCursors;
      }
      SDL_Cursor* sAppliedCursor = nullptr;

      SDL_Cursor* SdlCursorFor(HCURSOR cursor) noexcept
      {
        const auto key = reinterpret_cast<std::uintptr_t>(cursor);
        auto& cursors = Cursors();
        if (const auto it = cursors.find(key); it != cursors.end()) {
          return it->second;
        }
        if (key >= kFirstColorCursor) {
          return nullptr;
        }
        SDL_SystemCursor system = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (key) {
          case 32513: system = SDL_SYSTEM_CURSOR_TEXT; break;        // IDC_IBEAM
          case 32514: system = SDL_SYSTEM_CURSOR_WAIT; break;        // IDC_WAIT
          case 32515: system = SDL_SYSTEM_CURSOR_CROSSHAIR; break;   // IDC_CROSS
          case 32646: system = SDL_SYSTEM_CURSOR_MOVE; break;        // IDC_SIZEALL
          case 32648: system = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break; // IDC_NO
          case 32649: system = SDL_SYSTEM_CURSOR_POINTER; break;     // IDC_HAND
          case 32644: system = SDL_SYSTEM_CURSOR_EW_RESIZE; break;   // IDC_SIZEWE
          case 32645: system = SDL_SYSTEM_CURSOR_NS_RESIZE; break;   // IDC_SIZENS
          default: break;
        }
        SDL_Cursor* const created = SDL_CreateSystemCursor(system);
        if (created != nullptr) {
          cursors.emplace(key, created);
        }
        return created;
      }
    } // namespace

    POINT ScreenPixels(float x, float y) noexcept
    {
      const float scale = S().screenScale;
      return POINT{static_cast<LONG>(std::floor(x * scale + 1e-3f)), static_cast<LONG>(std::floor(y * scale + 1e-3f))};
    }

    void ScreenPoints(POINT pixels, float* x, float* y) noexcept
    {
      const float scale = S().screenScale;
      *x = static_cast<float>(pixels.x) / scale;
      *y = static_cast<float>(pixels.y) / scale;
    }

    void TranslateEvent(const SDL_Event& event) noexcept
    {
      const Uint32 type = event.type;
      if (type == S().wakeEventType) {
        return;
      }
      // FAF_SHELL_TRACE_INPUT=1 (debugging aid): window and display events, as SDL reports them.
      static const bool trace = [] {
        const char* const value = std::getenv("FAF_SHELL_TRACE_INPUT");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
      }();
      if (trace && ((type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST) ||
                    (type >= SDL_EVENT_DISPLAY_FIRST && type <= SDL_EVENT_DISPLAY_LAST) || type == SDL_EVENT_QUIT)) {
        std::fprintf(stderr, "[shell-event] SDL event 0x%x data1=%d data2=%d\n", static_cast<unsigned>(type),
                     static_cast<int>(event.window.data1), static_cast<int>(event.window.data2));
      }
      if (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST) {
        TranslateWindowEvent(event.window, type);
        return;
      }
      switch (type) {
        case SDL_EVENT_QUIT:
          TranslateQuit();
          break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
          TranslateKey(event.key);
          break;
        case SDL_EVENT_TEXT_INPUT:
          TranslateText(event.text);
          break;
        case SDL_EVENT_MOUSE_MOTION:
          TranslateMotion(event.motion);
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
          TranslateButton(event.button);
          break;
        case SDL_EVENT_MOUSE_WHEEL:
          TranslateWheel(event.wheel);
          break;
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_MOVED:
        case SDL_EVENT_DISPLAY_ORIENTATION:
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: {
          RefreshDisplays();
          ApplyDisplayScale();
          std::vector<HWND> tops;
          {
            std::lock_guard<std::recursive_mutex> guard(S().lock);
            tops = S().topLevels;
          }
          const LPARAM size = MAKELPARAM(static_cast<WORD>(win32::GetSystemMetrics(SM_CXSCREEN, nullptr)),
                                         static_cast<WORD>(win32::GetSystemMetrics(SM_CYSCREEN, nullptr)));
          for (HWND top : tops) {
            (void)Send(top, WM_DISPLAYCHANGE, 32, size);
          }
          break;
        }
        default:
          break;
      }
    }

    void NoteMessageRemoved(const MSG& message) noexcept
    {
      State& s = S();
      switch (message.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
          const bool down = message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
          const auto vk = static_cast<BYTE>(message.wParam);
          const bool repeat = (message.lParam & (1 << 30)) != 0;
          const bool toggle = down && !repeat && vk != VK_CAPITAL;
          const auto scan = static_cast<BYTE>((message.lParam >> 16) & 0xFF);
          const bool extended = (message.lParam & (1 << 24)) != 0;
          const BYTE specific = SpecificModifier(vk, scan, extended);
          if (specific != 0) {
            SetModifierKeys(s.syncKeys, vk, specific, down, toggle);
          } else {
            SetKey(s.syncKeys, vk, down, toggle);
          }
          break;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
          SetKey(s.syncKeys, VK_LBUTTON, true, false);
          break;
        case WM_LBUTTONUP:
          SetKey(s.syncKeys, VK_LBUTTON, false, false);
          break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
          SetKey(s.syncKeys, VK_RBUTTON, true, false);
          break;
        case WM_RBUTTONUP:
          SetKey(s.syncKeys, VK_RBUTTON, false, false);
          break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
          SetKey(s.syncKeys, VK_MBUTTON, true, false);
          break;
        case WM_MBUTTONUP:
          SetKey(s.syncKeys, VK_MBUTTON, false, false);
          break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
        case WM_XBUTTONUP:
          SetKey(s.syncKeys, HIWORD(message.wParam) == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1, message.message != WM_XBUTTONUP, false);
          break;
        default:
          break;
      }
    }

    void ResetKeys() noexcept
    {
      State& s = S();
      for (auto& key : s.syncKeys) {
        key.store(0, std::memory_order_relaxed);
      }
      for (auto& key : s.asyncKeys) {
        key.store(0, std::memory_order_relaxed);
      }
      s.mouseButtons = 0;
      if ((SDL_GetModState() & SDL_KMOD_CAPS) != 0) {
        s.syncKeys[VK_CAPITAL].store(0x01, std::memory_order_relaxed);
        s.asyncKeys[VK_CAPITAL].store(0x01, std::memory_order_relaxed);
      }
    }

    void ApplyCursor() noexcept
    {
      State& s = S();
      if (!s.active.load() || !IsMainThread()) {
        return;
      }
      const bool visible = s.cursor != nullptr && s.cursorShowCount >= 0;
      if (visible) {
        SDL_Cursor* const cursor = SdlCursorFor(s.cursor);
        if (cursor != nullptr && cursor != sAppliedCursor) {
          SDL_SetCursor(cursor);
          sAppliedCursor = cursor;
        }
        if (s.cursorHidden) {
          SDL_ShowCursor();
          s.cursorHidden = false;
        }
      } else if (!s.cursorHidden) {
        SDL_HideCursor();
        s.cursorHidden = true;
      }
    }

    void ApplyClip() noexcept
    {
      State& s = S();
      if (!s.active.load() || !IsMainThread()) {
        return;
      }
      std::vector<std::pair<SDL_Window*, SDL_Rect>> rects;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        for (HWND handle : s.topLevels) {
          WindowRecord* const top = Find(handle);
          if (top == nullptr || top->sdl == nullptr) {
            continue;
          }
          SDL_Rect rect{0, 0, 0, 0};
          if (s.clipped) {
            // The clip rectangle in this window's points, if it lies on the window.
            float l = 0.0f;
            float t = 0.0f;
            float r = 0.0f;
            float b = 0.0f;
            ScreenPoints(POINT{s.clip.left, s.clip.top}, &l, &t);
            ScreenPoints(POINT{s.clip.right, s.clip.bottom}, &r, &b);
            int w = 0;
            int h = 0;
            SDL_GetWindowSize(top->sdl, &w, &h);
            const int x0 = std::max(0, static_cast<int>(std::ceil(l - top->screenX)));
            const int y0 = std::max(0, static_cast<int>(std::ceil(t - top->screenY)));
            const int x1 = std::min(w, static_cast<int>(std::floor(r - top->screenX)));
            const int y1 = std::min(h, static_cast<int>(std::floor(b - top->screenY)));
            if (x1 > x0 && y1 > y0) {
              rect = SDL_Rect{x0, y0, x1 - x0, y1 - y0};
            }
          }
          rects.emplace_back(top->sdl, rect);
        }
      }
      for (const auto& [window, rect] : rects) {
        SDL_SetWindowMouseRect(window, rect.w > 0 ? &rect : nullptr);
      }
    }
  } // namespace detail

  HCURSOR CreateColorCursor(const std::uint32_t* pixels, const int size, const int hotX, const int hotY,
                            const float pixelsPerPoint)
  {
    using namespace detail;
    if (!IsActive() || !IsMainThread() || pixels == nullptr || size <= 0) {
      return nullptr;
    }
    SDL_Surface* const full = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888);
    if (full == nullptr) {
      return nullptr;
    }
    for (int y = 0; y < size; ++y) {
      std::memcpy(static_cast<std::uint8_t*>(full->pixels) + static_cast<std::size_t>(full->pitch) * y,
                  pixels + static_cast<std::size_t>(size) * y, static_cast<std::size_t>(size) * 4u);
    }
    SDL_Cursor* cursor = nullptr;
    if (pixelsPerPoint > 1.01f) {
      // The cursor's size in points is the base image's; the full-resolution pixels are the
      // high-density representation AppKit uses on Retina.
      const int small = std::max(1, static_cast<int>(std::lround(size / pixelsPerPoint)));
      SDL_Surface* const base = SDL_ScaleSurface(full, small, small, SDL_SCALEMODE_LINEAR);
      if (base != nullptr) {
        (void)SDL_AddSurfaceAlternateImage(base, full);
        cursor = SDL_CreateColorCursor(base, static_cast<int>(hotX / pixelsPerPoint), static_cast<int>(hotY / pixelsPerPoint));
        SDL_DestroySurface(base);
      }
    }
    if (cursor == nullptr) {
      cursor = SDL_CreateColorCursor(full, hotX, hotY);
    }
    SDL_DestroySurface(full);
    if (cursor == nullptr) {
      std::fprintf(stderr, "[shell] SDL_CreateColorCursor failed: %s\n", SDL_GetError());
      return nullptr;
    }
    const std::uintptr_t key = sNextColorCursor;
    sNextColorCursor += 4;
    Cursors().emplace(key, cursor);
    return reinterpret_cast<HCURSOR>(key);
  }

  namespace win32
  {
    using namespace detail;

    SHORT GetKeyState(const int nVirtKey) noexcept
    {
      if (nVirtKey < 0 || nVirtKey > 255) {
        return 0;
      }
      const std::uint8_t value = S().syncKeys[static_cast<std::size_t>(nVirtKey)].load(std::memory_order_relaxed);
      const int toggled = value & 0x01;
      return static_cast<SHORT>((value & 0x80) != 0 ? (0xFF80 | toggled) : toggled);
    }

    SHORT GetAsyncKeyState(const int vKey) noexcept
    {
      if (vKey < 0 || vKey > 255) {
        return 0;
      }
      return static_cast<SHORT>((S().asyncKeys[static_cast<std::size_t>(vKey)].load(std::memory_order_relaxed) & 0x80) != 0 ? 0x8000 : 0);
    }

    BOOL GetKeyboardState(const PBYTE lpKeyState) noexcept
    {
      if (lpKeyState == nullptr) {
        return FALSE;
      }
      for (std::size_t i = 0; i < 256; ++i) {
        lpKeyState[i] = S().syncKeys[i].load(std::memory_order_relaxed);
      }
      return TRUE;
    }

    UINT MapVirtualKeyW(const UINT uCode, const UINT uMapType) noexcept
    {
      switch (uMapType) {
        case MAPVK_VK_TO_VSC:
        case MAPVK_VSC_TO_VK:
        case MAPVK_VSC_TO_VK_EX:
          for (int sc = SDL_SCANCODE_A; sc < SDL_SCANCODE_COUNT; ++sc) {
            const KeyCode code = FromScancode(static_cast<SDL_Scancode>(sc));
            if (code.vk == 0) {
              continue;
            }
            if (uMapType == MAPVK_VK_TO_VSC && code.vk == uCode) {
              return code.scan;
            }
            if (uMapType != MAPVK_VK_TO_VSC && code.scan == uCode && !code.extended) {
              const BYTE specific = SpecificModifier(code.vk, code.scan, false);
              return uMapType == MAPVK_VSC_TO_VK_EX && specific != 0 ? specific : code.vk;
            }
          }
          return 0;
        case MAPVK_VK_TO_CHAR: {
          if ((uCode >= '0' && uCode <= '9') || (uCode >= 'A' && uCode <= 'Z') || uCode == VK_SPACE) {
            return uCode;
          }
          if (uCode >= VK_NUMPAD0 && uCode <= VK_NUMPAD9) {
            return '0' + (uCode - VK_NUMPAD0);
          }
          for (int sc = SDL_SCANCODE_A; sc < SDL_SCANCODE_COUNT; ++sc) {
            const KeyCode code = FromScancode(static_cast<SDL_Scancode>(sc));
            if (code.vk == uCode && code.vk != 0) {
              const SDL_Keycode key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
              return key > 0 && key < 0x10000 && (key & SDLK_SCANCODE_MASK) == 0 ? static_cast<UINT>(key) : 0;
            }
          }
          return 0;
        }
        default:
          return 0;
      }
    }

    HCURSOR SetCursor(const HCURSOR hCursor) noexcept
    {
      State& s = S();
      const HCURSOR previous = s.cursor;
      s.cursor = hCursor;
      ApplyCursor();
      return previous;
    }

    HCURSOR GetCursor() noexcept
    {
      return S().cursor;
    }

    int ShowCursor(const BOOL bShow) noexcept
    {
      State& s = S();
      s.cursorShowCount += bShow ? 1 : -1;
      ApplyCursor();
      return s.cursorShowCount;
    }

    BOOL GetCursorPos(const LPPOINT lpPoint) noexcept
    {
      if (lpPoint == nullptr) {
        return FALSE;
      }
      float x = 0.0f;
      float y = 0.0f;
      GlobalMouse(&x, &y);
      *lpPoint = ScreenPixels(x, y);
      return TRUE;
    }

    BOOL SetCursorPos(int X, int Y) noexcept
    {
      State& s = S();
      if (s.clipped) {
        X = std::clamp<int>(X, s.clip.left, std::max<int>(s.clip.left, s.clip.right - 1));
        Y = std::clamp<int>(Y, s.clip.top, std::max<int>(s.clip.top, s.clip.bottom - 1));
      }
      float x = 0.0f;
      float y = 0.0f;
      ScreenPoints(POINT{X, Y}, &x, &y);
      if (TestInputWarp(x, y)) {
        return TRUE; // a synthetic-input test never moves the user's pointer
      }
      return SDL_WarpMouseGlobal(x, y) ? TRUE : FALSE;
    }

    BOOL GetCursorInfo(const PCURSORINFO pci) noexcept
    {
      if (pci == nullptr) {
        return FALSE;
      }
      State& s = S();
      pci->flags = s.cursor != nullptr && s.cursorShowCount >= 0 ? CURSOR_SHOWING : 0;
      pci->hCursor = s.cursor;
      return win32::GetCursorPos(&pci->ptScreenPos);
    }

    BOOL ClipCursor(const RECT* const lpRect) noexcept
    {
      State& s = S();
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        s.clipped = lpRect != nullptr;
        if (lpRect != nullptr) {
          s.clip = *lpRect;
        }
      }
      ApplyClip();
      return TRUE;
    }

    BOOL GetClipCursor(const LPRECT lpRect) noexcept
    {
      if (lpRect == nullptr) {
        return FALSE;
      }
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      if (s.clipped) {
        *lpRect = s.clip;
        return TRUE;
      }
      lpRect->left = win32::GetSystemMetrics(SM_XVIRTUALSCREEN, nullptr);
      lpRect->top = win32::GetSystemMetrics(SM_YVIRTUALSCREEN, nullptr);
      lpRect->right = lpRect->left + win32::GetSystemMetrics(SM_CXVIRTUALSCREEN, nullptr);
      lpRect->bottom = lpRect->top + win32::GetSystemMetrics(SM_CYVIRTUALSCREEN, nullptr);
      return TRUE;
    }

    BOOL DestroyCursor(const HCURSOR hCursor) noexcept
    {
      const auto key = reinterpret_cast<std::uintptr_t>(hCursor);
      if (key < kFirstColorCursor) {
        return TRUE; // system cursors are shared
      }
      auto& cursors = Cursors();
      const auto it = cursors.find(key);
      if (it == cursors.end()) {
        return FALSE;
      }
      if (S().cursor == hCursor) {
        // Still on screen: SDL goes back to its default cursor when it is freed.
        S().cursor = ::LoadCursorW(nullptr, IDC_ARROW);
      }
      if (sAppliedCursor == it->second) {
        sAppliedCursor = nullptr;
      }
      SDL_DestroyCursor(it->second);
      cursors.erase(it);
      ApplyCursor();
      return TRUE;
    }

    UINT GetDoubleClickTime() noexcept
    {
      return 500;
    }
  } // namespace win32
} // namespace faf_posix::shell
