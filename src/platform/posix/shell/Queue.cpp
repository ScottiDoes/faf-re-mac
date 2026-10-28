// The main thread's message queue while the shell is active.
//
// SDL's events become messages here: input (WM_KEY*, WM_CHAR, WM_MOUSE*) is queued, window
// changes are sent to the window procedures as they are translated (Input.cpp). Events are
// taken from SDL one at a time while the queue is empty, so a sent message keeps its place
// among the input around it. PeekMessage/GetMessage follow Windows: posted and input
// messages in order, WM_QUIT once the queue is otherwise empty, then WM_PAINT for a window
// whose client area needs drawing. GetKeyState follows the key messages as they are
// removed; WM_SETCURSOR is sent as a mouse message is taken, as Windows does.

#include "platform/posix/shell/ShellState.h"

#include <algorithm>
#include <chrono>

namespace faf_posix::shell
{
  namespace detail
  {
    namespace
    {
      bool Matches(const MSG& message, HWND window, UINT first, UINT last) noexcept
      {
        if (window == reinterpret_cast<HWND>(-1)) {
          if (message.hwnd != nullptr) {
            return false;
          }
        } else if (window != nullptr && message.hwnd != window) {
          return false;
        }
        if (first == 0 && last == 0) {
          return true;
        }
        return message.message >= first && message.message <= last;
      }

      bool IsMouseMessage(UINT message) noexcept
      {
        return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL;
      }

      // The first visible window waiting for WM_PAINT (top-level windows in creation order,
      // then their children front to back).
      HWND FindInvalid(HWND filter) noexcept
      {
        State& s = S();
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        std::vector<WindowRecord*> stack;
        for (auto it = s.topLevels.rbegin(); it != s.topLevels.rend(); ++it) {
          if (WindowRecord* const top = Find(*it); top != nullptr && top->visible && !top->minimized) {
            stack.push_back(top);
          }
        }
        while (!stack.empty()) {
          WindowRecord* const record = stack.back();
          stack.pop_back();
          if (record->invalid && (filter == nullptr || filter == record->handle)) {
            return record->handle;
          }
          for (auto it = record->children.rbegin(); it != record->children.rend(); ++it) {
            if (WindowRecord* const child = Find(*it); child != nullptr && child->visible) {
              stack.push_back(child);
            }
          }
        }
        return nullptr;
      }

      bool TakeQueued(MSG* out, HWND window, UINT first, UINT last, bool remove) noexcept
      {
        State& s = S();
        std::lock_guard<std::mutex> guard(s.queueLock);
        for (auto it = s.queue.begin(); it != s.queue.end(); ++it) {
          if (Matches(*it, window, first, last)) {
            *out = *it;
            if (remove) {
              s.queue.erase(it);
            }
            return true;
          }
        }
        return false;
      }
    } // namespace

    DWORD Now() noexcept
    {
      using namespace std::chrono;
      return static_cast<DWORD>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    void Post(const MSG& message) noexcept
    {
      State& s = S();
      {
        std::lock_guard<std::mutex> guard(s.queueLock);
        // Windows coalesces mouse moves: a queued move to the same window is replaced.
        if (message.message == WM_MOUSEMOVE && !s.queue.empty() && s.queue.back().message == WM_MOUSEMOVE &&
            s.queue.back().hwnd == message.hwnd && s.queue.back().wParam == message.wParam) {
          s.queue.back() = message;
        } else {
          s.queue.push_back(message);
        }
      }
      if (s.queueEvent != nullptr) {
        (void)::SetEvent(s.queueEvent);
      }
      if (!IsMainThread()) {
        WakeMainThread();
      }
    }

    void PostFront(const MSG& message) noexcept
    {
      State& s = S();
      std::lock_guard<std::mutex> guard(s.queueLock);
      s.queue.push_front(message);
    }

    bool PaintPending() noexcept
    {
      return FindInvalid(nullptr) != nullptr;
    }

    bool SdlHasInput() noexcept
    {
      const Uint32 wake = S().wakeEventType;
      if (wake == 0) {
        return SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
      }
      return SDL_HasEvents(SDL_EVENT_FIRST, wake - 1) || SDL_HasEvents(wake + 1, SDL_EVENT_LAST);
    }

    void TranslatePending(const bool untilQueued) noexcept
    {
      State& s = S();
      SDL_Event event{};
      for (;;) {
        if (untilQueued) {
          std::lock_guard<std::mutex> guard(s.queueLock);
          if (!s.queue.empty()) {
            return;
          }
        }
        if (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) != 1) {
          return;
        }
        if (TestInputDrops(event)) {
          continue;
        }
        TranslateEvent(event);
      }
    }
  } // namespace detail

  namespace win32
  {
    using namespace detail;

    BOOL PeekMessageW(const LPMSG lpMsg, const HWND hWnd, const UINT wMsgFilterMin, const UINT wMsgFilterMax,
                      const UINT wRemoveMsg) noexcept
    {
      if (lpMsg == nullptr || !IsMainThread()) {
        return FALSE;
      }
      State& s = S();
      const bool remove = (wRemoveMsg & PM_REMOVE) != 0;
      MSG message{};
      bool found = TakeQueued(&message, hWnd, wMsgFilterMin, wMsgFilterMax, remove);
      if (!found) {
        SDL_PumpEvents();
        TranslatePending(true);
        found = TakeQueued(&message, hWnd, wMsgFilterMin, wMsgFilterMax, remove);
        if (!found) {
          // Messages for other windows or ranges may sit in front; translate the rest.
          TranslatePending(false);
          found = TakeQueued(&message, hWnd, wMsgFilterMin, wMsgFilterMax, remove);
        }
      }
      if (found) {
        *lpMsg = message;
        if (remove) {
          s.lastMessageTime = message.time;
          s.lastMessagePos = message.pt;
          NoteMessageRemoved(message);
          if (IsMouseMessage(message.message) && message.hwnd != nullptr &&
              (message.message == WM_MOUSEMOVE || message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN ||
               message.message == WM_MBUTTONDOWN || message.message == WM_XBUTTONDOWN || message.message == WM_LBUTTONDBLCLK ||
               message.message == WM_RBUTTONDBLCLK || message.message == WM_MBUTTONDBLCLK)) {
            bool captured = false;
            {
              std::lock_guard<std::recursive_mutex> guard(s.lock);
              captured = s.capture != nullptr;
            }
            if (!captured) {
              (void)Send(message.hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(message.hwnd),
                         MAKELPARAM(HTCLIENT, static_cast<WORD>(message.message)));
            }
          }
        }
        return TRUE;
      }

      const bool quitInRange = (wMsgFilterMin == 0 && wMsgFilterMax == 0) ||
                               (WM_QUIT >= wMsgFilterMin && WM_QUIT <= wMsgFilterMax);
      {
        std::lock_guard<std::mutex> guard(s.queueLock);
        if (s.quitPosted && quitInRange) {
          *lpMsg = MSG{};
          lpMsg->message = WM_QUIT;
          lpMsg->wParam = static_cast<WPARAM>(s.quitCode);
          lpMsg->time = Now();
          if (remove) {
            s.quitPosted = false;
          }
          return TRUE;
        }
      }

      const bool paintInRange = (wMsgFilterMin == 0 && wMsgFilterMax == 0) ||
                                (WM_PAINT >= wMsgFilterMin && WM_PAINT <= wMsgFilterMax);
      if (paintInRange && hWnd != reinterpret_cast<HWND>(-1)) {
        if (const HWND invalid = FindInvalid(hWnd)) {
          *lpMsg = MSG{};
          lpMsg->hwnd = invalid;
          lpMsg->message = WM_PAINT;
          lpMsg->time = Now();
          if (remove) {
            // Validated as it is taken: the stand-in's paint DCs do not call BeginPaint.
            {
              std::lock_guard<std::recursive_mutex> guard(s.lock);
              if (WindowRecord* const record = Find(invalid)) {
                record->invalid = false;
              }
            }
            TestInputFrame();
          }
          return TRUE;
        }
      }
      return FALSE;
    }

    BOOL GetMessageW(const LPMSG lpMsg, const HWND hWnd, const UINT wMsgFilterMin, const UINT wMsgFilterMax) noexcept
    {
      if (lpMsg == nullptr) {
        return -1;
      }
      for (;;) {
        if (win32::PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE) != FALSE) {
          return lpMsg->message != WM_QUIT ? TRUE : FALSE;
        }
        (void)WaitForEvent(-1);
      }
    }

    BOOL WaitMessage() noexcept
    {
      return WaitForEvent(-1) ? TRUE : FALSE;
    }

    LRESULT DispatchMessageW(const MSG* const lpMsg) noexcept
    {
      if (lpMsg == nullptr || lpMsg->hwnd == nullptr) {
        return 0;
      }
      return Send(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }

    BOOL TranslateMessage(const MSG* const lpMsg) noexcept
    {
      if (lpMsg == nullptr) {
        return FALSE;
      }
      const UINT message = lpMsg->message;
      if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) {
        return message == WM_KEYUP || message == WM_SYSKEYUP ? TRUE : FALSE;
      }
      // The characters SDL's text input does not produce: the control characters Windows'
      // TranslateMessage makes from Backspace, Tab, Enter, Escape and Ctrl+key. Printable
      // characters arrive from SDL as text and are queued as WM_CHAR after the key.
      const bool ctrl = (win32::GetKeyState(VK_CONTROL) & 0x8000) != 0;
      const bool alt = (lpMsg->lParam & (1 << 29)) != 0;
      WPARAM character = 0;
      switch (lpMsg->wParam) {
        case VK_BACK:
          character = ctrl ? 0x7F : 0x08;
          break;
        case VK_TAB:
          character = ctrl ? 0 : 0x09;
          break;
        case VK_RETURN:
          character = ctrl ? 0x0A : 0x0D;
          break;
        case VK_ESCAPE:
          character = 0x1B;
          break;
        case VK_CANCEL:
          character = 0x03;
          break;
        default:
          if (ctrl && !alt) {
            const WPARAM vk = lpMsg->wParam;
            if (vk >= 'A' && vk <= 'Z') {
              character = vk - 'A' + 1;
            } else if (vk == VK_OEM_4) {
              character = 0x1B;
            } else if (vk == VK_OEM_5) {
              character = 0x1C;
            } else if (vk == VK_OEM_6) {
              character = 0x1D;
            }
          }
          break;
      }
      if (character != 0) {
        MSG translated = *lpMsg;
        translated.message = message == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR;
        translated.wParam = character;
        PostFront(translated);
      }
      return TRUE;
    }

    void PostQuitMessage(const int nExitCode) noexcept
    {
      State& s = S();
      {
        std::lock_guard<std::mutex> guard(s.queueLock);
        s.quitPosted = true;
        s.quitCode = nExitCode;
      }
      if (s.queueEvent != nullptr) {
        (void)::SetEvent(s.queueEvent);
      }
    }

    BOOL PostMessageW(const HWND hWnd, const UINT Msg, const WPARAM wParam, const LPARAM lParam) noexcept
    {
      if (hWnd != nullptr && win32::IsWindow(hWnd) == FALSE) {
        ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
      }
      MSG message{};
      message.hwnd = hWnd;
      message.message = Msg;
      message.wParam = wParam;
      message.lParam = lParam;
      message.time = Now();
      message.pt = S().lastMessagePos;
      Post(message);
      return TRUE;
    }

    BOOL PostThreadMessageW(const DWORD idThread, const UINT Msg, const WPARAM wParam, const LPARAM lParam) noexcept
    {
      // Only the main thread has a queue.
      static_cast<void>(idThread);
      return win32::PostMessageW(nullptr, Msg, wParam, lParam);
    }

    LRESULT SendMessageW(const HWND hWnd, const UINT Msg, const WPARAM wParam, const LPARAM lParam) noexcept
    {
      return Send(hWnd, Msg, wParam, lParam);
    }

    DWORD GetQueueStatus(const UINT flags) noexcept
    {
      State& s = S();
      DWORD present = 0;
      {
        std::lock_guard<std::mutex> guard(s.queueLock);
        for (const MSG& message : s.queue) {
          if (message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) {
            present |= QS_KEY;
          } else if (message.message == WM_MOUSEMOVE) {
            present |= QS_MOUSEMOVE;
          } else if (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST) {
            present |= QS_MOUSEBUTTON;
          } else {
            present |= QS_POSTMESSAGE;
          }
        }
      }
      if (FindInvalid(nullptr) != nullptr) {
        present |= QS_PAINT;
      }
      present &= flags;
      return MAKELONG(present, present);
    }

    DWORD GetMessageTime() noexcept
    {
      return S().lastMessageTime;
    }

    DWORD GetMessagePos() noexcept
    {
      const POINT pt = S().lastMessagePos;
      return static_cast<DWORD>(MAKELONG(static_cast<WORD>(pt.x), static_cast<WORD>(pt.y)));
    }
  } // namespace win32
} // namespace faf_posix::shell
