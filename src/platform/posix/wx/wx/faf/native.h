#pragma once

// wx stand-in: native windows (see wx/faf/window.h). Everything here applies only to windows
// that have an HWND, i.e. while the window system is up (faf_posix::shell::IsActive());
// headless, windows have no handle and these functions leave them as they were.
//
// The window procedure and the Handle* family follow wx 2.4.2 msw/window.cpp
// (wxWindowMSW::MSWWindowProc): the messages the shell delivers become the wx events the
// library generates from them, sent to GetEventHandler() (so handlers pushed on a window,
// such as the UI's CMauiWxEventMapper and CUIKeyHandler on the game viewport, see them
// first). Key events carry wx's key code (wxCharCodeMSWToWX, else the virtual key), the raw
// virtual key and lParam (m_rawCode / m_rawFlags), Shift/Ctrl from GetKeyState and Alt from
// lParam bit 29; a handled key-down swallows the WM_CHAR that follows (wx's
// m_lastKeydownProcessed). Mouse events carry client pixels, the MK_ button and modifier
// flags, and wheel events screen coordinates, the rotation and WHEEL_DELTA.

#include "wx/faf/app.h"

#include "platform/posix/shell/ShellInternal.h"

#include <vector>

namespace faf_wx_detail
{
  // wxWndProc: the procedure of wx's window classes.
  inline LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    wxWindow* const window = wxFindWinFromHandle(reinterpret_cast<WXHWND>(hwnd));
    if (window == nullptr) {
      return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return static_cast<LRESULT>(window->MSWWindowProc(message, static_cast<WXWPARAM>(wParam), static_cast<WXLPARAM>(lParam)));
  }

  inline constexpr const wchar_t* kFrameClassName = L"wxWindowClass";
  inline constexpr const wchar_t* kChildClassName = L"wxWindowClassNR";

  // wxApp::RegisterWindowClasses: double clicks, the arrow as the class cursor.
  inline void RegisterWindowClasses()
  {
    static bool sRegistered = false;
    if (sRegistered) {
      return;
    }
    WNDCLASSW windowClass{};
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = &WndProc;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kFrameClassName;
    (void)::RegisterClassW(&windowClass);
    windowClass.lpszClassName = kChildClassName;
    (void)::RegisterClassW(&windowClass);
    sRegistered = true;
  }

  inline HWND Hwnd(const wxWindowBase* window)
  {
    return window != nullptr ? reinterpret_cast<HWND>(window->GetHandle()) : nullptr;
  }

  inline bool ShiftDown()
  {
    return ::GetKeyState(VK_SHIFT) < 0;
  }

  inline bool CtrlDown()
  {
    return ::GetKeyState(VK_CONTROL) < 0;
  }
} // namespace faf_wx_detail

// --- Geometry (wxWindowMSW::DoGetPosition / DoGetSize / DoGetClientSize / DoSetSize) -----------
inline void wxWindowBase::FafNativePosition(int* x, int* y) const
{
  const HWND hwnd = faf_wx_detail::Hwnd(this);
  if (hwnd == nullptr) {
    return;
  }
  RECT rect{};
  if (::GetWindowRect(hwnd, &rect) == FALSE) {
    return;
  }
  POINT point{rect.left, rect.top};
  // Children are placed in their parent's client area; top-level windows on the screen.
  if (!IsTopLevel() && m_parent != nullptr) {
    if (const HWND parent = faf_wx_detail::Hwnd(m_parent)) {
      ::ScreenToClient(parent, &point);
    }
  }
  *x = point.x;
  *y = point.y;
}

inline void wxWindowBase::FafNativeSize(int* width, int* height, const bool client) const
{
  const HWND hwnd = faf_wx_detail::Hwnd(this);
  if (hwnd == nullptr) {
    return;
  }
  RECT rect{};
  if ((client ? ::GetClientRect(hwnd, &rect) : ::GetWindowRect(hwnd, &rect)) == FALSE) {
    return;
  }
  *width = rect.right - rect.left;
  *height = rect.bottom - rect.top;
}

inline bool wxWindowBase::FafNativeSetSize(int x, int y, int width, int height, const int sizeFlags)
{
  const HWND hwnd = faf_wx_detail::Hwnd(this);
  if (hwnd == nullptr) {
    return false;
  }
  int currentX = 0;
  int currentY = 0;
  int currentW = 0;
  int currentH = 0;
  GetPosition(&currentX, &currentY);
  GetSize(&currentW, &currentH);
  if (x == currentX && y == currentY && width == currentW && height == currentH) {
    return true;
  }
  if (x == -1 && (sizeFlags & wxSIZE_ALLOW_MINUS_ONE) == 0) {
    x = currentX;
  }
  if (y == -1 && (sizeFlags & wxSIZE_ALLOW_MINUS_ONE) == 0) {
    y = currentY;
  }
  if (width == -1) {
    width = currentW; // the stand-in's best size is the current one
  }
  if (height == -1) {
    height = currentH;
  }
  m_x = x;
  m_y = y;
  m_width = width;
  m_height = height;
  ::MoveWindow(hwnd, x, y, width, height, TRUE);
  return true;
}

// wxWindowMSW::DoSetClientSize: the frame around the client area is measured and added, a
// few times over in case it changes with the size.
inline bool wxWindowBase::FafNativeSetClientSize(const int width, const int height)
{
  const HWND hwnd = faf_wx_detail::Hwnd(this);
  if (hwnd == nullptr) {
    return false;
  }
  for (int attempt = 0; attempt < 4; ++attempt) {
    RECT client{};
    ::GetClientRect(hwnd, &client);
    if ((client.right == width || width == -1) && (client.bottom == height || height == -1)) {
      break;
    }
    RECT window{};
    ::GetWindowRect(hwnd, &window);
    const int windowWidth = (width == -1 ? client.right : width) + (window.right - window.left - client.right);
    const int windowHeight = (height == -1 ? client.bottom : height) + (window.bottom - window.top - client.bottom);
    POINT point{window.left, window.top};
    if (!IsTopLevel() && m_parent != nullptr) {
      if (const HWND parent = faf_wx_detail::Hwnd(m_parent)) {
        ::ScreenToClient(parent, &point);
      }
    }
    ::MoveWindow(hwnd, point.x, point.y, windowWidth, windowHeight, TRUE);
  }
  return true;
}

inline void wxWindowBase::FafNativeShow(const bool show)
{
  if (const HWND hwnd = faf_wx_detail::Hwnd(this)) {
    ::ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
    if (show && IsTopLevel()) {
      ::BringWindowToTop(hwnd);
    }
  }
}

inline void wxWindowBase::FafNativeStyleChanged()
{
  const HWND hwnd = faf_wx_detail::Hwnd(this);
  if (hwnd == nullptr) {
    return;
  }
  WXDWORD exStyle = 0;
  const WXDWORD style = static_cast<const wxWindow*>(this)->MSWGetStyle(m_windowStyle, &exStyle);
  ::SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(static_cast<LONG>(style)));
  ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(static_cast<LONG>(exStyle)));
  ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The shell takes a window's size limits ahead of time (WM_GETMINMAXINFO when it is shown or
// its frame changes), so new hints are passed on with a frame change.
inline void wxWindowBase::FafNativeSizeHintsChanged()
{
  const HWND hwnd = faf_wx_detail::Hwnd(this);
  if (hwnd != nullptr && IsTopLevel()) {
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

// --- Creation and destruction (wxWindowMSW::MSWCreate / ~wxWindowMSW) ---------------------------
inline WXDWORD wxWindow::MSWGetStyle(long flags, WXDWORD* exstyle) const
{
  WXDWORD style = 0;
  WXDWORD exStyle = 0;
  if (IsTopLevel()) {
    // wxTopLevelWindowMSW::MSWGetStyle.
    style = (flags & wxFRAME_TOOL_WINDOW) != 0 ? WS_POPUP : WS_OVERLAPPED;
    if ((flags & wxRESIZE_BORDER) != 0) {
      style |= WS_THICKFRAME;
    } else if ((flags & wxNO_BORDER) == 0) {
      style |= WS_BORDER;
    } else {
      style |= WS_POPUP;
    }
    style |= (flags & wxCAPTION) != 0 ? WS_CAPTION : WS_POPUP;
    if ((flags & wxMINIMIZE_BOX) != 0) {
      style |= WS_MINIMIZEBOX;
    }
    if ((flags & wxMAXIMIZE_BOX) != 0) {
      style |= WS_MAXIMIZEBOX;
    }
    if ((flags & wxSYSTEM_MENU) != 0) {
      style |= WS_SYSMENU;
    }
    if ((flags & wxMINIMIZE) != 0) {
      style |= WS_MINIMIZE;
    }
    if ((flags & wxMAXIMIZE) != 0) {
      style |= WS_MAXIMIZE;
    }
    if ((flags & wxFRAME_TOOL_WINDOW) != 0) {
      exStyle |= WS_EX_TOOLWINDOW;
    }
    if ((flags & wxSTAY_ON_TOP) != 0) {
      exStyle |= WS_EX_TOPMOST;
    }
  } else {
    style = WS_CHILD | WS_CLIPSIBLINGS;
  }
  if ((flags & wxCLIP_CHILDREN) != 0) {
    style |= WS_CLIPCHILDREN;
  }
  if (exstyle != nullptr) {
    *exstyle = exStyle;
  }
  return style;
}

inline void wxWindow::FafCreateNative(const wxPoint& pos, const wxSize& size, const wxString& title)
{
  if (m_hWnd != 0 || !faf_wx_detail::NativeWindows() || !faf_posix::shell::IsMainThread()) {
    return;
  }
  const bool topLevel = IsTopLevel();
  const HWND parent = faf_wx_detail::Hwnd(m_parent);
  if (!topLevel && parent == nullptr) {
    return; // a child of a window that has no native window
  }
  faf_wx_detail::RegisterWindowClasses();
  WXDWORD exStyle = 0;
  WXDWORD style = MSWGetStyle(m_windowStyle, &exStyle);
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  if (topLevel) {
    // wxWindowMSW::MSWGetCreateWindowCoords: the system places and sizes what wx leaves at
    // its defaults. Top-level windows start hidden.
    x = pos.x == -1 ? CW_USEDEFAULT : pos.x;
    y = pos.x == -1 ? CW_USEDEFAULT : (pos.y == -1 ? 200 : pos.y);
    w = size.x == -1 ? CW_USEDEFAULT : size.x;
    h = size.x == -1 ? CW_USEDEFAULT : (size.y == -1 ? 250 : size.y);
  } else {
    if (m_isShown) {
      style |= WS_VISIBLE;
    }
    x = m_x;
    y = m_y;
    w = m_width;
    h = m_height;
  }
  const HWND hwnd = ::CreateWindowExW(exStyle, topLevel ? faf_wx_detail::kFrameClassName : faf_wx_detail::kChildClassName,
                                      title.c_str(), style, x, y, w, h, parent,
                                      topLevel ? nullptr : reinterpret_cast<HMENU>(static_cast<std::intptr_t>(m_windowId)),
                                      nullptr, nullptr);
  if (hwnd == nullptr) {
    return;
  }
  m_hWnd = reinterpret_cast<WXHWND>(hwnd);
  faf_wx_detail::HandleMap()[m_hWnd] = this;
}

inline void wxWindow::FafDestroyNative()
{
  if (m_hWnd == 0) {
    return;
  }
  const HWND hwnd = reinterpret_cast<HWND>(m_hWnd);
  // Dissociated first: this object is half destroyed, its messages go to DefWindowProc.
  faf_wx_detail::HandleMap().erase(m_hWnd);
  m_hWnd = 0;
  ::DestroyWindow(hwnd);
}

// --- The window procedure (wxWindowMSW::MSWWindowProc) ------------------------------------------
inline long wxWindow::MSWDefWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
  if (m_hWnd == 0) {
    return 0;
  }
  return static_cast<long>(::DefWindowProcW(reinterpret_cast<HWND>(m_hWnd), nMsg, static_cast<WPARAM>(wParam),
                                            static_cast<LPARAM>(lParam)));
}

inline long wxWindow::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
  if (m_hWnd == 0) {
    return MSWDefWindowProc(nMsg, wParam, lParam);
  }
  bool processed = false;
  long result = 0;
  const int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
  const int y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
  switch (nMsg) {
    case WM_DESTROY:
      processed = true;
      break;

    case WM_MOVE:
      processed = FafHandleMove(x, y);
      break;

    case WM_SIZE:
      switch (wParam) {
        case SIZE_MAXHIDE:
        case SIZE_MAXSHOW:
          break;
        case SIZE_MINIMIZED:
          // No size event for a minimized window (its client area is empty); frames tell
          // their handlers they were iconized.
          if (IsTopLevel() && !m_fafIconized) {
            m_fafIconized = true;
            wxIconizeEvent event(m_windowId, true);
            event.SetEventObject(this);
            processed = GetEventHandler()->ProcessEvent(event);
          }
          break;
        default:
          if (IsTopLevel() && m_fafIconized) {
            m_fafIconized = false;
            wxIconizeEvent event(m_windowId, false);
            event.SetEventObject(this);
            (void)GetEventHandler()->ProcessEvent(event);
          }
          processed = FafHandleSize(LOWORD(lParam), HIWORD(lParam), static_cast<WXUINT>(wParam));
          break;
      }
      break;

    case WM_ACTIVATE:
      processed = FafHandleActivate(LOWORD(wParam), HIWORD(wParam) != 0, static_cast<WXHWND>(lParam));
      break;

    case WM_SETFOCUS:
      processed = FafHandleSetFocus(static_cast<WXHWND>(wParam));
      break;

    case WM_KILLFOCUS:
      processed = FafHandleKillFocus(static_cast<WXHWND>(wParam));
      break;

    case WM_SHOWWINDOW:
      processed = FafHandleShow(wParam != 0);
      break;

    case WM_PAINT:
      processed = FafHandlePaint();
      break;

    case WM_CLOSE:
      // A frame or dialog asks its handlers (wxFrame / wxDialog::MSWWindowProc); either way
      // DefWindowProc must not destroy the window - ~wxWindow does.
      if (IsTopLevel()) {
        (void)Close();
      }
      processed = true;
      result = TRUE;
      break;

    case WM_ACTIVATEAPP:
      if (wxTheApp != nullptr) {
        wxTheApp->SetActive(wParam != 0, FindFocus());
      }
      break;

    case WM_GETMINMAXINFO:
      processed = FafHandleGetMinMaxInfo(reinterpret_cast<void*>(lParam));
      break;

    case WM_SETCURSOR:
      processed = FafHandleSetCursor(static_cast<short>(LOWORD(lParam)));
      if (processed) {
        result = TRUE;
      }
      break;

    case WM_MOUSEMOVE:
      processed = FafHandleMouseMove(x, y, static_cast<WXUINT>(wParam));
      break;

    case WM_LBUTTONDOWN:
      // The window takes the focus when clicked.
      if (AcceptsFocus()) {
        SetFocus();
      }
      [[fallthrough]];
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
      processed = FafHandleMouseEvent(nMsg, x, y, static_cast<WXUINT>(wParam));
      break;

    case WM_MOUSEWHEEL:
      processed = FafHandleMouseWheel(wParam, lParam);
      break;

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
      m_lastKeydownProcessed = false;
      if (FafHandleKeyDown(wParam, lParam)) {
        processed = true;
        m_lastKeydownProcessed = true;
        break;
      }
      switch (wParam) {
        // Not interesting to OnChar: tell the system they were handled.
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:
        case VK_CAPITAL:
        case VK_NUMLOCK:
        case VK_SCROLL:
          processed = true;
          break;
        // Their characters come as WM_CHAR (TranslateMessage); the default procedure still
        // sees the key.
        case VK_ESCAPE:
        case VK_SPACE:
        case VK_RETURN:
        case VK_BACK:
        case VK_TAB:
        case VK_ADD:
        case VK_SUBTRACT:
        case VK_MULTIPLY:
        case VK_DIVIDE:
        case VK_OEM_1:
        case VK_OEM_2:
        case VK_OEM_3:
        case VK_OEM_4:
        case VK_OEM_5:
        case VK_OEM_6:
        case VK_OEM_7:
        case VK_OEM_PLUS:
        case VK_OEM_COMMA:
        case VK_OEM_MINUS:
        case VK_OEM_PERIOD:
          processed = false;
          break;
        default:
          // Keys without a character (arrows, function keys, ...) get their wxEVT_CHAR here.
          processed = FafHandleChar(wParam, lParam, false);
          break;
      }
      break;

    case WM_SYSKEYUP:
    case WM_KEYUP:
      processed = FafHandleKeyUp(wParam, lParam);
      break;

    case WM_SYSCHAR:
    case WM_CHAR:
      if (m_lastKeydownProcessed) {
        // A key-down handled by a handler suppresses its character, by design in wx.
        m_lastKeydownProcessed = false;
        processed = true;
      } else {
        processed = FafHandleChar(wParam, lParam, true);
      }
      break;

    default:
      break;
  }
  if (!processed) {
    result = MSWDefWindowProc(nMsg, wParam, lParam);
  }
  return result;
}

// --- Handlers (wxWindowMSW::Handle*) -------------------------------------------------------------
inline bool wxWindow::FafHandleSize(const int width, const int height, const WXUINT flag)
{
  (void)flag;
  wxSizeEvent event(wxSize(width, height), m_windowId);
  event.SetEventObject(this);
  if (GetEventHandler()->ProcessEvent(event)) {
    return true;
  }
  return FafDefaultOnSize(event);
}

inline bool wxWindow::FafHandleMove(const int x, const int y)
{
  wxMoveEvent event(wxPoint(x, y), m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafHandleActivate(const int state, const bool minimized, const WXHWND other)
{
  (void)minimized;
  (void)other;
  wxActivateEvent event(wxEVT_ACTIVATE, state == WA_ACTIVE || state == WA_CLICKACTIVE, m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafHandleSetFocus(const WXHWND previous)
{
  faf_wx_detail::FocusWindow() = this;
  wxFocusEvent event(wxEVT_SET_FOCUS, m_windowId);
  event.SetEventObject(this);
  event.SetWindow(wxFindWinFromHandle(previous));
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafHandleKillFocus(const WXHWND next)
{
  if (faf_wx_detail::FocusWindow() == this) {
    faf_wx_detail::FocusWindow() = nullptr;
  }
  wxFocusEvent event(wxEVT_KILL_FOCUS, m_windowId);
  event.SetEventObject(this);
  event.SetWindow(wxFindWinFromHandle(next));
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafHandleShow(const bool show)
{
  wxShowEvent event(GetId(), show);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafHandlePaint()
{
  wxPaintEvent event(m_windowId);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

// The application decides first (wxEVT_SET_CURSOR); the stand-in's wxCursor has no native
// cursor, so the class cursor (DefWindowProc) or the window procedure's own handling (the
// game viewport's) follows.
inline bool wxWindow::FafHandleSetCursor(const int hitTest)
{
  if (hitTest != HTCLIENT) {
    return false;
  }
  POINT pt{};
  ::GetCursorPos(&pt);
  int x = pt.x;
  int y = pt.y;
  ScreenToClient(&x, &y);
  wxSetCursorEvent event(x, y);
  event.SetEventObject(this);
  (void)GetEventHandler()->ProcessEvent(event);
  return false;
}

inline bool wxWindow::FafHandleGetMinMaxInfo(void* const info)
{
  auto* const minMax = static_cast<MINMAXINFO*>(info);
  bool handled = false;
  if (minMax == nullptr) {
    return false;
  }
  if (GetMinWidth() != -1) {
    minMax->ptMinTrackSize.x = GetMinWidth();
    handled = true;
  }
  if (GetMinHeight() != -1) {
    minMax->ptMinTrackSize.y = GetMinHeight();
    handled = true;
  }
  if (GetMaxWidth() != -1) {
    minMax->ptMaxTrackSize.x = GetMaxWidth();
    handled = true;
  }
  if (GetMaxHeight() != -1) {
    minMax->ptMaxTrackSize.y = GetMaxHeight();
    handled = true;
  }
  return handled;
}

inline void wxWindow::FafInitMouseEvent(wxMouseEvent& event, const int x, const int y, const WXUINT flags)
{
  const wxPoint origin = GetClientAreaOrigin();
  event.m_x = x - origin.x;
  event.m_y = y - origin.y;
  event.m_shiftDown = (flags & MK_SHIFT) != 0;
  event.m_controlDown = (flags & MK_CONTROL) != 0;
  event.m_leftDown = (flags & MK_LBUTTON) != 0;
  event.m_middleDown = (flags & MK_MBUTTON) != 0;
  event.m_rightDown = (flags & MK_RBUTTON) != 0;
  event.m_altDown = ::GetKeyState(VK_MENU) < 0;
  event.SetTimestamp(static_cast<long>(::GetMessageTime()));
  event.SetEventObject(this);
  event.SetId(m_windowId);
  m_lastMouseX = x;
  m_lastMouseY = y;
  m_lastMouseEvent = event.GetEventType();
}

inline bool wxWindow::FafHandleMouseEvent(const WXUINT msg, const int x, const int y, const WXUINT flags)
{
  // WM_MOUSEMOVE .. WM_MBUTTONDBLCLK are consecutive.
  static constexpr wxEventType kEvents[] = {wxEVT_MOTION,      wxEVT_LEFT_DOWN,  wxEVT_LEFT_UP,
                                            wxEVT_LEFT_DCLICK, wxEVT_RIGHT_DOWN, wxEVT_RIGHT_UP,
                                            wxEVT_RIGHT_DCLICK, wxEVT_MIDDLE_DOWN, wxEVT_MIDDLE_UP,
                                            wxEVT_MIDDLE_DCLICK};
  if (msg < WM_MOUSEMOVE || msg > WM_MBUTTONDBLCLK) {
    return false;
  }
  wxMouseEvent event(kEvents[msg - WM_MOUSEMOVE]);
  FafInitMouseEvent(event, x, y, flags);
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafIsMouseInWindow() const
{
  POINT pt{};
  ::GetCursorPos(&pt);
  HWND hwnd = ::WindowFromPoint(pt);
  while (hwnd != nullptr && hwnd != reinterpret_cast<HWND>(m_hWnd)) {
    hwnd = ::GetParent(hwnd);
  }
  return hwnd != nullptr;
}

inline bool wxWindow::FafHandleMouseMove(const int x, const int y, const WXUINT flags)
{
  if (!m_mouseInWindow) {
    // A move does not mean the mouse is inside when it is captured.
    if (!HasCapture() || FafIsMouseInWindow()) {
      m_mouseInWindow = true;
      wxMouseEvent event(wxEVT_ENTER_WINDOW);
      FafInitMouseEvent(event, x, y, flags);
      (void)GetEventHandler()->ProcessEvent(event);
    }
  }
  // wxUSE_MOUSEEVENT_HACK: the move that follows a click at the same place is dropped.
  if ((m_lastMouseEvent == wxEVT_RIGHT_DOWN || m_lastMouseEvent == wxEVT_LEFT_DOWN ||
       m_lastMouseEvent == wxEVT_MIDDLE_DOWN) &&
      m_lastMouseX == x && m_lastMouseY == y) {
    m_lastMouseEvent = wxEVT_MOTION;
    return false;
  }
  return FafHandleMouseEvent(WM_MOUSEMOVE, x, y, flags);
}

inline bool wxWindow::FafHandleMouseWheel(const WXWPARAM wParam, const WXLPARAM lParam)
{
  wxMouseEvent event(wxEVT_MOUSEWHEEL);
  // Wheel messages carry screen coordinates; wx passes them on as they are.
  FafInitMouseEvent(event, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)), LOWORD(wParam));
  event.m_wheelRotation = static_cast<short>(HIWORD(wParam));
  event.m_wheelDelta = WHEEL_DELTA;
  UINT lines = 3;
  (void)::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
  event.m_linesPerAction = static_cast<int>(lines);
  return GetEventHandler()->ProcessEvent(event);
}

inline void wxWindow::FafCheckMouseLeave()
{
  if (!m_mouseInWindow || m_hWnd == 0 || FafIsMouseInWindow()) {
    return;
  }
  m_mouseInWindow = false;
  WXUINT state = 0;
  if (faf_wx_detail::ShiftDown()) {
    state |= MK_SHIFT;
  }
  if (faf_wx_detail::CtrlDown()) {
    state |= MK_CONTROL;
  }
  if (::GetKeyState(VK_LBUTTON) < 0) {
    state |= MK_LBUTTON;
  }
  if (::GetKeyState(VK_MBUTTON) < 0) {
    state |= MK_MBUTTON;
  }
  if (::GetKeyState(VK_RBUTTON) < 0) {
    state |= MK_RBUTTON;
  }
  POINT pt{};
  ::GetCursorPos(&pt);
  // Window coordinates, for symmetry with wxEVT_ENTER_WINDOW (as wx does it).
  RECT rect{};
  ::GetWindowRect(reinterpret_cast<HWND>(m_hWnd), &rect);
  wxMouseEvent event(wxEVT_LEAVE_WINDOW);
  FafInitMouseEvent(event, pt.x - rect.left, pt.y - rect.top, state);
  (void)GetEventHandler()->ProcessEvent(event);
}

inline wxKeyEvent wxWindow::FafCreateKeyEvent(const wxEventType type, const int id, const WXLPARAM lParam,
                                              const WXWPARAM wParam) const
{
  wxKeyEvent event(type);
  event.SetId(GetId());
  event.m_shiftDown = faf_wx_detail::ShiftDown();
  event.m_controlDown = faf_wx_detail::CtrlDown();
  event.m_altDown = (HIWORD(lParam) & KF_ALTDOWN) == KF_ALTDOWN;
  event.m_eventObject = const_cast<wxWindow*>(this);
  event.m_keyCode = id;
  event.m_rawCode = static_cast<wxUint32>(wParam);
  event.m_rawFlags = static_cast<wxUint32>(lParam);
  event.SetTimestamp(static_cast<long>(::GetMessageTime()));
  // The cursor, in window coordinates.
  POINT pt{};
  ::GetCursorPos(&pt);
  RECT rect{};
  ::GetWindowRect(reinterpret_cast<HWND>(m_hWnd), &rect);
  event.m_x = pt.x - rect.left;
  event.m_y = pt.y - rect.top;
  return event;
}

// isASCII: from WM_CHAR (a character), else from WM_KEYDOWN (a key without one).
inline bool wxWindow::FafHandleChar(const WXWPARAM wParam, const WXLPARAM lParam, const bool isASCII)
{
  int id = 0;
  if (isASCII) {
    // Ctrl+letter makes 1..26; Enter, Backspace and Tab are their special codes.
    id = static_cast<int>(wParam);
    switch (id) {
      case 13:
        id = WXK_RETURN;
        break;
      case 8:
        id = WXK_BACK;
        break;
      case 9:
        id = WXK_TAB;
        break;
      default:
        break;
    }
  } else {
    id = wxCharCodeMSWToWX(static_cast<int>(wParam));
    if (id == 0) {
      return false; // a character key: its WM_CHAR follows
    }
  }
  wxKeyEvent event(FafCreateKeyEvent(wxEVT_CHAR, id, lParam, wParam));
  // AltGr characters come with Ctrl and Alt down: no modifiers for them.
  if (event.m_controlDown && event.m_altDown && id >= 32 && id < 256) {
    event.m_controlDown = false;
    event.m_altDown = false;
  }
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafHandleKeyDown(const WXWPARAM wParam, const WXLPARAM lParam)
{
  int id = wxCharCodeMSWToWX(static_cast<int>(wParam));
  if (id == 0) {
    id = static_cast<int>(wParam);
  }
  wxKeyEvent event(FafCreateKeyEvent(wxEVT_KEY_DOWN, id, lParam, wParam));
  return GetEventHandler()->ProcessEvent(event);
}

inline bool wxWindow::FafHandleKeyUp(const WXWPARAM wParam, const WXLPARAM lParam)
{
  int id = wxCharCodeMSWToWX(static_cast<int>(wParam));
  if (id == 0) {
    id = static_cast<int>(wParam);
  }
  wxKeyEvent event(FafCreateKeyEvent(wxEVT_KEY_UP, id, lParam, wParam));
  return GetEventHandler()->ProcessEvent(event);
}

// --- The application's side (wxApp::SendIdleEvents, wxApp::ProcessMessage) ----------------------
inline bool wxApp::SendIdleEvents(wxWindow* window)
{
  if (window == nullptr) {
    return false;
  }
  wxIdleEvent event;
  event.SetEventObject(window);
  // wxWindowMSW's own EVT_IDLE row, when no handler took the event: the leave check.
  if (!window->GetEventHandler()->ProcessEvent(event)) {
    window->FafCheckMouseLeave();
  }
  bool needMore = event.MoreRequested();
  std::vector<wxWindow*> children(window->GetChildren().begin(), window->GetChildren().end());
  for (wxWindow* child : children) {
    if (window->GetChildren().Member(child) && SendIdleEvents(child)) {
      needMore = true;
    }
  }
  return needMore;
}

inline bool wxApp::ProcessMessage(WXMSG* message)
{
  if (message == nullptr) {
    return false;
  }
  wxWindow* const target = wxFindWinFromHandle(reinterpret_cast<WXHWND>(message->hwnd));
  if (target == nullptr) {
    return false;
  }
  // Accelerators first, up to the first top-level window.
  for (wxWindow* window = target; window != nullptr; window = window->GetParent()) {
    if (window->MSWTranslateMessage(message)) {
      return true;
    }
    if (window->IsTopLevel()) {
      break;
    }
  }
  // Then the parents' hooks (keyboard navigation).
  for (wxWindow* window = target->GetParent(); window != nullptr; window = window->GetParent()) {
    if (window->MSWProcessMessage(message)) {
      return true;
    }
  }
  return false;
}
