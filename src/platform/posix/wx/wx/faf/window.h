#pragma once

// wx stand-in: wxWindow, sizers, validators and accelerators (see wx/defs.h).
//
// Windows keep wx's ownership rules (a parent deletes its children, a window deletes its
// sizer, top-level windows are destroyed on the next idle).
//
// Two modes, as the platform layer's Window area has them:
// - Window system active (faf_posix::shell::IsActive(), the game): every window gets a native
//   HWND when it is created (CreateWindowEx through the Window area: a top-level window is an
//   SDL window, a child a rectangle in it), and behaves as wx 2.4.2 MSW: geometry, visibility,
//   focus and capture are the native window's; the window procedure (wx/faf/native.h) turns
//   the messages the shell delivers into wx events the way wxWindowMSW::MSWWindowProc does
//   (keys, characters, mouse, wheel, size, move, activation, focus, close, paint, cursor).
// - Otherwise (headless runs): no native window exists. GetHandle() is 0, MSWWindowProc only
//   answers like DefWindowProc, the windows remember the geometry and state they are given,
//   and no size, move or paint events are generated.

#include "wx/faf/event.h"

#include "platform/posix/shell/Shell.h"

#include <unordered_map>

class wxWindow;
class wxSizer;
class wxStaticBox;
class wxNotebook;

namespace faf_wx_detail
{
  inline wxWindow*& FocusWindow()
  {
    static wxWindow* sFocus = nullptr;
    return sFocus;
  }
  inline wxWindow*& CaptureWindow()
  {
    static wxWindow* sCapture = nullptr;
    return sCapture;
  }
  inline int& LastControlId()
  {
    static int sLastId = -200;
    return sLastId;
  }

  // True while the window system is up: windows then have native HWNDs.
  inline bool NativeWindows()
  {
    return faf_posix::shell::IsActive();
  }

  // wxWinHandleHash: HWND -> wxWindow (never destroyed, windows unlink at exit).
  inline std::unordered_map<WXHWND, wxWindow*>& HandleMap()
  {
    static auto* const sMap = new std::unordered_map<WXHWND, wxWindow*>();
    return *sMap;
  }
} // namespace faf_wx_detail

inline wxWindow* wxFindWinFromHandle(WXHWND hwnd)
{
  const auto& map = faf_wx_detail::HandleMap();
  const auto it = map.find(hwnd);
  return it != map.end() ? it->second : nullptr;
}

// Never destroyed, so windows with static storage can unlink themselves at exit.
inline wxWindowList& wxTopLevelWindows = *new wxWindowList();
inline wxList& wxPendingDelete = *new wxList();

class wxValidator : public wxEvtHandler
{
public:
  wxValidator() = default;
  virtual wxObject* Clone() const { return nullptr; }
  virtual bool Validate(wxWindow* parent)
  {
    (void)parent;
    return true;
  }
  virtual bool TransferToWindow() { return true; }
  virtual bool TransferFromWindow() { return true; }
  wxWindow* GetWindow() const { return m_validatorWindow; }
  void SetWindow(wxWindow* window) { m_validatorWindow = window; }
  static bool IsSilent() { return true; }
  static void SetBellOnError(bool doIt = true) { (void)doIt; }

protected:
  wxWindow* m_validatorWindow = nullptr;
};

inline const wxValidator wxDefaultValidator;

class wxToolTip : public wxObject
{
public:
  explicit wxToolTip(const wxString& tip) : m_text(tip) {}
  void SetTip(const wxString& tip) { m_text = tip; }
  const wxString& GetTip() const { return m_text; }
  static void Enable(bool flag) { (void)flag; }
  static void SetDelay(long milliseconds) { (void)milliseconds; }

private:
  wxString m_text;
};

class wxAcceleratorEntry
{
public:
  wxAcceleratorEntry(int flags = 0, int keyCode = 0, int cmd = 0) : m_flags(flags), m_keyCode(keyCode), m_command(cmd)
  {}
  void Set(int flags, int keyCode, int cmd)
  {
    m_flags = flags;
    m_keyCode = keyCode;
    m_command = cmd;
  }
  int GetFlags() const { return m_flags; }
  int GetKeyCode() const { return m_keyCode; }
  int GetCommand() const { return m_command; }

private:
  int m_flags;
  int m_keyCode;
  int m_command;
};

class wxAcceleratorTable : public wxObject
{
public:
  wxAcceleratorTable() = default;
  wxAcceleratorTable(int n, const wxAcceleratorEntry entries[]) : m_entries(entries, entries + n) {}
  bool Ok() const { return !m_entries.empty(); }
  const std::vector<wxAcceleratorEntry>& GetEntries() const { return m_entries; }

private:
  std::vector<wxAcceleratorEntry> m_entries;
};

inline wxAcceleratorTable wxNullAcceleratorTable;

class wxWindowBase : public wxEvtHandler
{
public:
  wxWindowBase() { m_eventHandler = this; }
  wxWindowBase(const wxWindowBase&) = delete;
  wxWindowBase& operator=(const wxWindowBase&) = delete;

  ~wxWindowBase() override;

  // --- creation and destruction ---
  bool IsBeingDeleted() const { return m_isBeingDeleted; }
  virtual bool Destroy()
  {
    delete this;
    return true;
  }
  bool DestroyChildren();
  bool Close(bool force = false);

  // --- identity and labels ---
  void SetId(wxWindowID winid) { m_windowId = winid; }
  wxWindowID GetId() const { return m_windowId; }
  static int NewControlId() { return --faf_wx_detail::LastControlId(); }
  static int NextControlId(int winid) { return winid - 1; }
  static int PrevControlId(int winid) { return winid + 1; }
  virtual void SetTitle(const wxString& title) { m_title = title; }
  virtual wxString GetTitle() const { return m_title; }
  virtual void SetLabel(const wxString& label) { SetTitle(label); }
  virtual wxString GetLabel() const { return GetTitle(); }
  virtual void SetName(const wxString& name) { m_windowName = name; }
  virtual wxString GetName() const { return m_windowName; }

  // --- geometry ---
  void SetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO)
  {
    DoSetSize(x, y, width, height, sizeFlags);
  }
  void SetSize(int width, int height) { DoSetSize(-1, -1, width, height, wxSIZE_USE_EXISTING); }
  void SetSize(const wxSize& size) { SetSize(size.x, size.y); }
  void SetSize(const wxRect& rect, int sizeFlags = wxSIZE_AUTO)
  {
    DoSetSize(rect.x, rect.y, rect.width, rect.height, sizeFlags);
  }
  void Move(int x, int y) { DoSetSize(x, y, -1, -1, wxSIZE_USE_EXISTING); }
  void Move(const wxPoint& pt) { Move(pt.x, pt.y); }
  void SetPosition(const wxPoint& pt) { Move(pt); }
  void SetClientSize(int width, int height) { DoSetClientSize(width, height); }
  void SetClientSize(const wxSize& size) { DoSetClientSize(size.x, size.y); }
  void SetClientSize(const wxRect& rect) { SetClientSize(rect.width, rect.height); }
  void GetPosition(int* x, int* y) const
  {
    int px = m_x;
    int py = m_y;
    FafNativePosition(&px, &py);
    if (x != nullptr) {
      *x = px;
    }
    if (y != nullptr) {
      *y = py;
    }
  }
  wxPoint GetPosition() const
  {
    int x = 0;
    int y = 0;
    GetPosition(&x, &y);
    return wxPoint(x, y);
  }
  void GetSize(int* width, int* height) const
  {
    int w = m_width;
    int h = m_height;
    FafNativeSize(&w, &h, false);
    if (width != nullptr) {
      *width = w;
    }
    if (height != nullptr) {
      *height = h;
    }
  }
  wxSize GetSize() const
  {
    int w = 0;
    int h = 0;
    GetSize(&w, &h);
    return wxSize(w, h);
  }
  wxRect GetRect() const { return wxRect(GetPosition(), GetSize()); }
  void GetClientSize(int* width, int* height) const
  {
    int w = m_width;
    int h = m_height;
    FafNativeSize(&w, &h, true);
    if (width != nullptr) {
      *width = w;
    }
    if (height != nullptr) {
      *height = h;
    }
  }
  wxSize GetClientSize() const
  {
    int w = 0;
    int h = 0;
    GetClientSize(&w, &h);
    return wxSize(w, h);
  }
  virtual wxPoint GetClientAreaOrigin() const { return wxPoint(0, 0); }
  wxRect GetClientRect() const { return wxRect(GetClientAreaOrigin(), GetClientSize()); }
  wxSize GetBestSize() const { return GetSize(); }
  void SetBestFittingSize(const wxSize& size = wxDefaultSize) { (void)size; }
  void Centre(int direction = wxBOTH) { (void)direction; }
  void Center(int direction = wxBOTH) { Centre(direction); }
  void CentreOnParent(int dir = wxBOTH) { Centre(dir); }
  void CenterOnParent(int dir = wxBOTH) { Centre(dir); }
  void CentreOnScreen(int dir = wxBOTH) { Centre(dir); }
  void CenterOnScreen(int dir = wxBOTH) { Centre(dir); }
  virtual void Fit() {}
  virtual void FitInside() {}
  virtual void SetSizeHints(int minW, int minH, int maxW = -1, int maxH = -1, int incW = -1, int incH = -1)
  {
    (void)incW;
    (void)incH;
    m_minWidth = minW;
    m_minHeight = minH;
    m_maxWidth = maxW;
    m_maxHeight = maxH;
    FafNativeSizeHintsChanged();
  }
  void SetSizeHints(const wxSize& minSize, const wxSize& maxSize = wxDefaultSize, const wxSize& incSize = wxDefaultSize)
  {
    SetSizeHints(minSize.x, minSize.y, maxSize.x, maxSize.y, incSize.x, incSize.y);
  }
  virtual void SetVirtualSizeHints(int minW, int minH, int maxW = -1, int maxH = -1)
  {
    (void)minW;
    (void)minH;
    (void)maxW;
    (void)maxH;
  }
  int GetMinWidth() const { return m_minWidth; }
  int GetMinHeight() const { return m_minHeight; }
  int GetMaxWidth() const { return m_maxWidth; }
  int GetMaxHeight() const { return m_maxHeight; }
  void SetVirtualSize(const wxSize& size) { (void)size; }
  wxSize GetVirtualSize() const { return GetClientSize(); }

  // --- visibility and state ---
  virtual bool Show(bool show = true)
  {
    if (show == m_isShown) {
      return false;
    }
    m_isShown = show;
    FafNativeShow(show);
    return true;
  }
  bool Hide() { return Show(false); }
  virtual bool Enable(bool enable = true)
  {
    if (enable == m_isEnabled) {
      return false;
    }
    m_isEnabled = enable;
    if (GetHandle() != 0) {
      ::EnableWindow(reinterpret_cast<HWND>(GetHandle()), enable ? TRUE : FALSE);
    }
    return true;
  }
  bool Disable() { return Enable(false); }
  bool IsShown() const { return m_isShown; }
  bool IsEnabled() const { return m_isEnabled; }
  virtual void SetWindowStyleFlag(long style)
  {
    m_windowStyle = style;
    FafNativeStyleChanged();
  }
  virtual long GetWindowStyleFlag() const { return m_windowStyle; }
  void SetWindowStyle(long style) { SetWindowStyleFlag(style); }
  long GetWindowStyle() const { return GetWindowStyleFlag(); }
  bool HasFlag(int flag) const { return (m_windowStyle & flag) != 0; }
  virtual bool IsRetained() const { return false; }
  virtual void SetExtraStyle(long exStyle) { m_exStyle = exStyle; }
  long GetExtraStyle() const { return m_exStyle; }
  virtual void MakeModal(bool modal = true) { (void)modal; }
  virtual void SetThemeEnabled(bool enable) { (void)enable; }
  virtual bool GetThemeEnabled() const { return false; }
  virtual bool IsTopLevel() const { return false; }

  // --- focus and capture ---
  // With native windows these are the HWND's (::SetFocus, ::GetFocus, ::SetCapture), as in
  // wx MSW; the wx-side records follow the WM_SETFOCUS / WM_KILLFOCUS the shell sends.
  virtual void SetFocus()
  {
    if (GetHandle() != 0) {
      ::SetFocus(reinterpret_cast<HWND>(GetHandle()));
      return;
    }
    faf_wx_detail::FocusWindow() = reinterpret_cast<wxWindow*>(this);
  }
  virtual void SetFocusFromKbd() { SetFocus(); }
  static wxWindow* FindFocus()
  {
    if (faf_wx_detail::NativeWindows()) {
      return wxFindWinFromHandle(reinterpret_cast<WXHWND>(::GetFocus()));
    }
    return faf_wx_detail::FocusWindow();
  }
  virtual bool AcceptsFocus() const { return IsShown() && IsEnabled(); }
  virtual bool AcceptsFocusFromKeyboard() const { return AcceptsFocus(); }
  void CaptureMouse()
  {
    if (GetHandle() != 0) {
      ::SetCapture(reinterpret_cast<HWND>(GetHandle()));
    }
    faf_wx_detail::CaptureWindow() = reinterpret_cast<wxWindow*>(this);
  }
  void ReleaseMouse()
  {
    if (GetHandle() != 0 && ::GetCapture() == reinterpret_cast<HWND>(GetHandle())) {
      ::ReleaseCapture();
    }
    if (faf_wx_detail::CaptureWindow() == reinterpret_cast<wxWindow*>(this)) {
      faf_wx_detail::CaptureWindow() = nullptr;
    }
  }
  static wxWindow* GetCapture()
  {
    if (faf_wx_detail::NativeWindows()) {
      return wxFindWinFromHandle(reinterpret_cast<WXHWND>(::GetCapture()));
    }
    return faf_wx_detail::CaptureWindow();
  }
  virtual bool HasCapture() const { return GetCapture() == reinterpret_cast<const wxWindow*>(this); }

  // --- parent and children ---
  wxWindowList& GetChildren() { return m_children; }
  const wxWindowList& GetChildren() const { return m_children; }
  wxWindow* GetParent() const { return reinterpret_cast<wxWindow*>(m_parent); }
  wxWindow* GetGrandParent() const { return m_parent != nullptr ? m_parent->GetParent() : nullptr; }
  void SetParent(wxWindowBase* parent) { m_parent = parent; }
  virtual bool Reparent(wxWindowBase* newParent)
  {
    if (newParent == m_parent) {
      return false;
    }
    if (m_parent != nullptr) {
      m_parent->RemoveChild(this);
    }
    if (newParent != nullptr) {
      newParent->AddChild(this);
    }
    return true;
  }
  virtual void AddChild(wxWindowBase* child)
  {
    m_children.Append(reinterpret_cast<wxWindow*>(child));
    child->SetParent(this);
  }
  virtual void RemoveChild(wxWindowBase* child)
  {
    m_children.DeleteObject(reinterpret_cast<wxWindow*>(child));
    child->SetParent(nullptr);
  }
  wxWindow* FindWindow(long winid);
  wxWindow* FindWindow(const wxString& name);

  // --- event handlers ---
  wxEvtHandler* GetEventHandler() const { return m_eventHandler; }
  void SetEventHandler(wxEvtHandler* handler) { m_eventHandler = handler; }
  void PushEventHandler(wxEvtHandler* handler)
  {
    wxEvtHandler* const top = GetEventHandler();
    handler->SetNextHandler(top);
    top->SetPreviousHandler(handler);
    SetEventHandler(handler);
  }
  wxEvtHandler* PopEventHandler(bool deleteHandler = false)
  {
    wxEvtHandler* const top = GetEventHandler();
    if (top == this) {
      return nullptr;
    }
    wxEvtHandler* const next = top->GetNextHandler();
    if (next != nullptr) {
      next->SetPreviousHandler(nullptr);
    }
    top->SetNextHandler(nullptr);
    SetEventHandler(next != nullptr ? next : this);
    if (deleteHandler) {
      delete top;
      return nullptr;
    }
    return top;
  }
  bool RemoveEventHandler(wxEvtHandler* handler)
  {
    wxEvtHandler* previous = nullptr;
    for (wxEvtHandler* current = GetEventHandler(); current != nullptr && current != this;
         current = current->GetNextHandler()) {
      if (current == handler) {
        wxEvtHandler* const next = current->GetNextHandler();
        if (previous == nullptr) {
          SetEventHandler(next);
        } else {
          previous->SetNextHandler(next);
        }
        if (next != nullptr) {
          next->SetPreviousHandler(previous);
        }
        current->SetNextHandler(nullptr);
        current->SetPreviousHandler(nullptr);
        return true;
      }
      previous = current;
    }
    return false;
  }

  // --- validators and dialog data ---
  virtual void SetValidator(const wxValidator& validator)
  {
    delete m_validator;
    m_validator = static_cast<wxValidator*>(validator.Clone());
    if (m_validator != nullptr) {
      m_validator->SetWindow(reinterpret_cast<wxWindow*>(this));
    }
  }
  wxValidator* GetValidator() { return m_validator; }
  virtual bool Validate() { return true; }
  virtual bool TransferDataToWindow() { return true; }
  virtual bool TransferDataFromWindow() { return true; }
  virtual void InitDialog()
  {
    wxInitDialogEvent event(GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
  }
  virtual void SetAcceleratorTable(const wxAcceleratorTable& accel) { m_acceleratorTable = accel; }
  wxAcceleratorTable* GetAcceleratorTable() { return &m_acceleratorTable; }

  // --- coordinates ---
  void ClientToScreen(int* x, int* y) const
  {
    if (GetHandle() != 0) {
      POINT pt{x != nullptr ? *x : 0, y != nullptr ? *y : 0};
      ::ClientToScreen(reinterpret_cast<HWND>(GetHandle()), &pt);
      if (x != nullptr) {
        *x = pt.x;
      }
      if (y != nullptr) {
        *y = pt.y;
      }
      return;
    }
    int originX = 0;
    int originY = 0;
    ScreenOrigin(&originX, &originY);
    if (x != nullptr) {
      *x += originX;
    }
    if (y != nullptr) {
      *y += originY;
    }
  }
  wxPoint ClientToScreen(const wxPoint& pt) const
  {
    int x = pt.x;
    int y = pt.y;
    ClientToScreen(&x, &y);
    return wxPoint(x, y);
  }
  void ScreenToClient(int* x, int* y) const
  {
    if (GetHandle() != 0) {
      POINT pt{x != nullptr ? *x : 0, y != nullptr ? *y : 0};
      ::ScreenToClient(reinterpret_cast<HWND>(GetHandle()), &pt);
      if (x != nullptr) {
        *x = pt.x;
      }
      if (y != nullptr) {
        *y = pt.y;
      }
      return;
    }
    int originX = 0;
    int originY = 0;
    ScreenOrigin(&originX, &originY);
    if (x != nullptr) {
      *x -= originX;
    }
    if (y != nullptr) {
      *y -= originY;
    }
  }
  wxPoint ScreenToClient(const wxPoint& pt) const
  {
    int x = pt.x;
    int y = pt.y;
    ScreenToClient(&x, &y);
    return wxPoint(x, y);
  }
  virtual void WarpPointer(int x, int y)
  {
    if (GetHandle() != 0) {
      ClientToScreen(&x, &y);
      ::SetCursorPos(x, y);
    }
  }

  // --- painting and appearance ---
  virtual void Refresh(bool eraseBackground = true, const wxRect* rect = nullptr)
  {
    if (GetHandle() != 0) {
      RECT r{};
      if (rect != nullptr) {
        r = RECT{rect->x, rect->y, rect->x + rect->width, rect->y + rect->height};
      }
      ::InvalidateRect(reinterpret_cast<HWND>(GetHandle()), rect != nullptr ? &r : nullptr, eraseBackground ? TRUE : FALSE);
    }
  }
  void RefreshRect(const wxRect& rect) { Refresh(true, &rect); }
  virtual void Update()
  {
    if (GetHandle() != 0) {
      ::UpdateWindow(reinterpret_cast<HWND>(GetHandle()));
    }
  }
  // Z-order of a native window (wxWindow::Raise/Lower).
  virtual void Raise()
  {
    if (GetHandle() != 0) {
      ::BringWindowToTop(reinterpret_cast<HWND>(GetHandle()));
    }
  }
  virtual void Lower()
  {
    if (GetHandle() != 0) {
      ::SetWindowPos(reinterpret_cast<HWND>(GetHandle()), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
  virtual void Clear() {}
  virtual void Freeze() {}
  virtual void Thaw() {}
  virtual void PrepareDC(wxDC& dc) { (void)dc; }
  virtual bool SetBackgroundColour(const wxColour& colour)
  {
    m_backgroundColour = colour;
    return true;
  }
  wxColour GetBackgroundColour() const { return m_backgroundColour; }
  virtual bool SetForegroundColour(const wxColour& colour)
  {
    m_foregroundColour = colour;
    return true;
  }
  wxColour GetForegroundColour() const { return m_foregroundColour; }
  virtual bool SetCursor(const wxCursor& cursor)
  {
    m_cursor = cursor;
    return true;
  }
  const wxCursor& GetCursor() const { return m_cursor; }
  virtual bool SetFont(const wxFont& font)
  {
    m_font = font;
    return true;
  }
  wxFont& GetFont() { return m_font; }
  const wxFont& GetFont() const { return m_font; }
  virtual int GetCharHeight() const { return 16; }
  virtual int GetCharWidth() const { return 8; }
  virtual void GetTextExtent(
    const wxString& text,
    int* x,
    int* y,
    int* descent = nullptr,
    int* externalLeading = nullptr,
    const wxFont* font = nullptr
  ) const
  {
    (void)font;
    if (x != nullptr) {
      *x = static_cast<int>(text.Len()) * GetCharWidth();
    }
    if (y != nullptr) {
      *y = GetCharHeight();
    }
    if (descent != nullptr) {
      *descent = 0;
    }
    if (externalLeading != nullptr) {
      *externalLeading = 0;
    }
  }
  bool IsExposed(int x, int y) const
  {
    (void)x;
    (void)y;
    return false;
  }

  // --- scrolling ---
  virtual void SetScrollbar(int orient, int pos, int thumbVisible, int range, bool refresh = true)
  {
    (void)thumbVisible;
    (void)range;
    (void)refresh;
    SetScrollPos(orient, pos);
  }
  virtual void SetScrollPos(int orient, int pos, bool refresh = true)
  {
    (void)refresh;
    (orient == wxHORIZONTAL ? m_scrollPosX : m_scrollPosY) = pos;
  }
  virtual int GetScrollPos(int orient) const { return orient == wxHORIZONTAL ? m_scrollPosX : m_scrollPosY; }
  virtual int GetScrollThumb(int orient) const
  {
    (void)orient;
    return 0;
  }
  virtual int GetScrollRange(int orient) const
  {
    (void)orient;
    return 0;
  }
  virtual void ScrollWindow(int dx, int dy, const wxRect* rect = nullptr)
  {
    (void)dx;
    (void)dy;
    (void)rect;
  }
  virtual bool ScrollLines(int lines)
  {
    (void)lines;
    return false;
  }
  virtual bool ScrollPages(int pages)
  {
    (void)pages;
    return false;
  }
  bool LineUp() { return ScrollLines(-1); }
  bool LineDown() { return ScrollLines(1); }
  bool PageUp() { return ScrollPages(-1); }
  bool PageDown() { return ScrollPages(1); }

  // --- tooltips, drag and drop ---
  void SetToolTip(const wxString& tip)
  {
    delete m_toolTip;
    m_toolTip = new wxToolTip(tip);
  }
  void SetToolTip(wxToolTip* tip)
  {
    delete m_toolTip;
    m_toolTip = tip;
  }
  wxToolTip* GetToolTip() const { return m_toolTip; }
  virtual void SetDropTarget(wxDropTarget* dropTarget) { (void)dropTarget; }
  virtual void DragAcceptFiles(bool accept) { (void)accept; }
  void SetHelpText(const wxString& text) { (void)text; }

  // --- layout ---
  void SetAutoLayout(bool autoLayout) { m_autoLayout = autoLayout; }
  bool GetAutoLayout() const { return m_autoLayout; }
  virtual bool Layout();
  void SetSizer(wxSizer* sizer, bool deleteOld = true);
  void SetSizerAndFit(wxSizer* sizer, bool deleteOld = true)
  {
    SetSizer(sizer, deleteOld);
    Fit();
  }
  wxSizer* GetSizer() const { return m_windowSizer; }
  void SetContainingSizer(wxSizer* sizer) { m_containingSizer = sizer; }
  wxSizer* GetContainingSizer() const { return m_containingSizer; }
  void SetConstraints(wxLayoutConstraints* constraints);
  wxLayoutConstraints* GetConstraints() const { return m_constraints; }

  // --- menus, native handle, idle ---
  virtual bool PopupMenu(wxMenu* menu, const wxPoint& pos)
  {
    (void)menu;
    (void)pos;
    return false;
  }
  bool PopupMenu(wxMenu* menu, int x, int y) { return PopupMenu(menu, wxPoint(x, y)); }
  virtual WXWidget GetHandle() const = 0;
  virtual void OnInternalIdle() {}

protected:
  bool TryParent(wxEvent& event) override;

  // What the library's own close handlers do when the window's tables leave the close event
  // unhandled (wxTopLevelWindowBase::OnCloseWindow, wxDialog::OnCloseWindow). A plain window
  // has none.
  virtual bool FafHandleUnprocessedClose(wxCloseEvent& event)
  {
    (void)event;
    return false;
  }

  // The native window's geometry, read and set as wx MSW does (wx/faf/native.h). They leave
  // the arguments alone for a window without an HWND.
  void FafNativePosition(int* x, int* y) const;
  void FafNativeSize(int* width, int* height, bool client) const;
  bool FafNativeSetSize(int x, int y, int width, int height, int sizeFlags);
  bool FafNativeSetClientSize(int width, int height);
  void FafNativeShow(bool show);
  void FafNativeStyleChanged();
  void FafNativeSizeHintsChanged();

  virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags)
  {
    if (FafNativeSetSize(x, y, width, height, sizeFlags)) {
      return;
    }
    const bool allowMinusOne = (sizeFlags & wxSIZE_ALLOW_MINUS_ONE) != 0;
    if (x != -1 || allowMinusOne) {
      m_x = x;
    }
    if (y != -1 || allowMinusOne) {
      m_y = y;
    }
    if (width != -1) {
      m_width = width;
    }
    if (height != -1) {
      m_height = height;
    }
  }
  virtual void DoSetClientSize(int width, int height)
  {
    if (FafNativeSetClientSize(width, height)) {
      return;
    }
    DoSetSize(-1, -1, width, height, wxSIZE_USE_EXISTING);
  }
  void ScreenOrigin(int* x, int* y) const
  {
    *x = 0;
    *y = 0;
    for (const wxWindowBase* window = this; window != nullptr; window = window->m_parent) {
      *x += window->m_x;
      *y += window->m_y;
      if (window->IsTopLevel()) {
        break;
      }
    }
  }
  bool CreateBase(
    wxWindowBase* parent,
    wxWindowID winid,
    const wxPoint& pos,
    const wxSize& size,
    long style,
    const wxString& name,
    int defaultWidth,
    int defaultHeight
  )
  {
    m_windowId = winid == -1 ? NewControlId() : winid;
    m_windowStyle = style;
    m_windowName = name;
    m_x = pos.x == -1 ? 0 : pos.x;
    m_y = pos.y == -1 ? 0 : pos.y;
    m_width = size.x == -1 ? defaultWidth : size.x;
    m_height = size.y == -1 ? defaultHeight : size.y;
    if (parent != nullptr && !IsTopLevel()) {
      parent->AddChild(this);
    } else {
      m_parent = parent;
    }
    return true;
  }

  wxWindowID m_windowId = -1;
  wxWindowBase* m_parent = nullptr;
  wxWindowList m_children;
  wxEvtHandler* m_eventHandler = nullptr;
  wxValidator* m_validator = nullptr;
  wxAcceleratorTable m_acceleratorTable;
  wxString m_windowName;
  wxString m_title;
  long m_windowStyle = 0;
  long m_exStyle = 0;
  bool m_isShown = true;
  bool m_isEnabled = true;
  bool m_isBeingDeleted = false;
  bool m_autoLayout = false;
  wxColour m_backgroundColour;
  wxColour m_foregroundColour;
  wxFont m_font;
  wxCursor m_cursor;
  wxSizer* m_windowSizer = nullptr;
  wxSizer* m_containingSizer = nullptr;
  wxLayoutConstraints* m_constraints = nullptr;
  wxToolTip* m_toolTip = nullptr;
  int m_x = 0;
  int m_y = 0;
  int m_width = 0;
  int m_height = 0;
  int m_minWidth = -1;
  int m_minHeight = -1;
  int m_maxWidth = -1;
  int m_maxHeight = -1;
  int m_scrollPosX = 0;
  int m_scrollPosY = 0;
};

// wx 2.4.2 MSW: wxWindowMSW is wxWindow (wx/window.h #defines one as the other).
class wxWindow : public wxWindowBase
{
public:
  wxWindow() = default;
  wxWindow(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxPanelNameStr
  )
  {
    Create(parent, winid, pos, size, style, name);
  }
  ~wxWindow() override
  {
    DestroyChildren();
    FafDestroyNative();
  }

  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxPanelNameStr
  )
  {
    const bool created = CreateBase(parent, winid, pos, size, style, name, 20, 20);
    FafCreateNative(pos, size, wxEmptyString);
    return created;
  }

  WXWidget GetHandle() const override { return GetHWND(); }
  WXHWND GetHWND() const { return m_hWnd; }
  void SetHWND(WXHWND hWnd) { m_hWnd = hWnd; }
  void AssociateHandle(WXWidget handle) { m_hWnd = handle; }
  void DissociateHandle() { m_hWnd = 0; }

  // wxWindowMSW::MSWWindowProc / MSWDefWindowProc / MSWGetStyle (wx/faf/native.h). Without a
  // native window the procedure answers like DefWindowProc, as before the window system.
  virtual long MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam);
  virtual long MSWDefWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam);
  virtual WXDWORD MSWGetStyle(long flags, WXDWORD* exstyle = nullptr) const;
  virtual bool MSWProcessMessage(WXMSG* msg)
  {
    (void)msg;
    return false;
  }
  virtual bool MSWTranslateMessage(WXMSG* msg)
  {
    (void)msg;
    return false;
  }

  // wxWindowMSW::OnInternalIdle's part that matters here: the wxEVT_LEAVE_WINDOW the
  // library generates from idle time (wx/faf/native.h).
  void FafCheckMouseLeave();

protected:
  // CreateWindowEx for this window when the window system is up (wx/faf/native.h).
  void FafCreateNative(const wxPoint& pos, const wxSize& size, const wxString& title);
  void FafDestroyNative();

  // The wxWindowMSW::Handle* family (wx 2.4.2 msw/window.cpp).
  bool FafHandleSize(int width, int height, WXUINT flag);
  bool FafHandleMove(int x, int y);
  bool FafHandleActivate(int state, bool minimized, WXHWND other);
  bool FafHandleSetFocus(WXHWND previous);
  bool FafHandleKillFocus(WXHWND next);
  bool FafHandleShow(bool show);
  bool FafHandlePaint();
  bool FafHandleSetCursor(int hitTest);
  bool FafHandleGetMinMaxInfo(void* info);
  void FafInitMouseEvent(wxMouseEvent& event, int x, int y, WXUINT flags);
  bool FafHandleMouseEvent(WXUINT msg, int x, int y, WXUINT flags);
  bool FafHandleMouseMove(int x, int y, WXUINT flags);
  bool FafHandleMouseWheel(WXWPARAM wParam, WXLPARAM lParam);
  wxKeyEvent FafCreateKeyEvent(wxEventType type, int id, WXLPARAM lParam, WXWPARAM wParam) const;
  bool FafHandleChar(WXWPARAM wParam, WXLPARAM lParam, bool isASCII);
  bool FafHandleKeyDown(WXWPARAM wParam, WXLPARAM lParam);
  bool FafHandleKeyUp(WXWPARAM wParam, WXLPARAM lParam);
  bool FafIsMouseInWindow() const;
  // What the library's own tables do with a size or idle event nobody handled
  // (wxTopLevelWindowBase::OnSize lays the client area out).
  virtual bool FafDefaultOnSize(wxSizeEvent& event)
  {
    (void)event;
    return false;
  }

  WXHWND m_hWnd = 0;
  bool m_mouseInWindow = false;
  bool m_lastKeydownProcessed = false;
  int m_lastMouseX = -1;
  int m_lastMouseY = -1;
  wxEventType m_lastMouseEvent = wxEVT_NULL;
  bool m_fafIconized = false;
};

inline bool wxWindowBase::DestroyChildren()
{
  while (!m_children.IsEmpty()) {
    wxWindow* const child = m_children.GetFirst();
    m_children.DeleteObject(child);
    child->SetParent(nullptr);
    delete child;
  }
  return true;
}

inline bool wxWindowBase::Close(bool force)
{
  wxCloseEvent event(wxEVT_CLOSE_WINDOW, m_windowId);
  event.SetEventObject(this);
  event.SetCanVeto(!force);
  if (GetEventHandler()->ProcessEvent(event)) {
    return !event.GetVeto();
  }
  return FafHandleUnprocessedClose(event) && !event.GetVeto();
}

inline bool wxWindowBase::TryParent(wxEvent& event)
{
  if (event.IsCommandEvent() && !IsTopLevel() && m_parent != nullptr && !m_parent->IsBeingDeleted()) {
    return m_parent->GetEventHandler()->ProcessEvent(event);
  }
  return false;
}

inline wxWindow* wxWindowBase::FindWindow(long winid)
{
  if (m_windowId == winid) {
    return reinterpret_cast<wxWindow*>(this);
  }
  for (wxWindow* child : m_children) {
    if (wxWindow* const found = child->FindWindow(winid)) {
      return found;
    }
  }
  return nullptr;
}

inline wxWindow* wxWindowBase::FindWindow(const wxString& name)
{
  if (m_windowName == name) {
    return reinterpret_cast<wxWindow*>(this);
  }
  for (wxWindow* child : m_children) {
    if (wxWindow* const found = child->FindWindow(name)) {
      return found;
    }
  }
  return nullptr;
}

inline wxWindow* wxFindWindowById(long winid, const wxWindow* parent = nullptr)
{
  if (parent != nullptr) {
    return const_cast<wxWindow*>(parent)->FindWindow(winid);
  }
  for (wxWindow* window : wxTopLevelWindows) {
    if (wxWindow* const found = window->FindWindow(winid)) {
      return found;
    }
  }
  return nullptr;
}

inline wxWindow* wxGetTopLevelParent(wxWindow* window)
{
  while (window != nullptr && !window->IsTopLevel()) {
    window = window->GetParent();
  }
  return window;
}

inline wxWindow* wxGetActiveWindow()
{
  if (faf_wx_detail::NativeWindows()) {
    return wxFindWinFromHandle(reinterpret_cast<WXHWND>(::GetActiveWindow()));
  }
  return wxWindow::FindFocus();
}

// --- Device contexts bound to a window -------------------------------------------------------
class wxWindowDC : public wxDC
{
public:
  wxWindowDC() = default;
  explicit wxWindowDC(wxWindow* window) : m_owner(window)
  {
    if (window != nullptr) {
      SetFafSize(window->GetSize().x, window->GetSize().y);
    }
  }
  wxWindow* GetWindow() const { return m_owner; }

protected:
  wxWindow* m_owner = nullptr;
};

class wxClientDC : public wxWindowDC
{
public:
  wxClientDC() = default;
  explicit wxClientDC(wxWindow* window) : wxWindowDC(window)
  {
    if (window != nullptr) {
      SetFafSize(window->GetClientSize().x, window->GetClientSize().y);
    }
  }
};

class wxPaintDC : public wxClientDC
{
public:
  wxPaintDC() = default;
  explicit wxPaintDC(wxWindow* window) : wxClientDC(window) {}
};

// --- Sizers (wx/sizer.h). Items and owned sizers are kept; positions are not computed. ------
class wxSizerItem : public wxObject
{
public:
  wxSizerItem(wxWindow* window, int option, int flag, int border, wxObject* userData)
    : m_window(window), m_option(option), m_flag(flag), m_border(border), m_userData(userData)
  {
    if (window != nullptr) {
      m_minSize = window->GetSize();
    }
  }
  wxSizerItem(wxSizer* sizer, int option, int flag, int border, wxObject* userData)
    : m_sizer(sizer), m_option(option), m_flag(flag), m_border(border), m_userData(userData)
  {}
  wxSizerItem(int width, int height, int option, int flag, int border, wxObject* userData)
    : m_size(width, height), m_minSize(width, height), m_option(option), m_flag(flag), m_border(border),
      m_userData(userData)
  {}
  ~wxSizerItem() override;

  bool IsWindow() const { return m_window != nullptr; }
  bool IsSizer() const { return m_sizer != nullptr; }
  bool IsSpacer() const { return m_window == nullptr && m_sizer == nullptr; }
  wxWindow* GetWindow() const { return m_window; }
  wxSizer* GetSizer() const { return m_sizer; }
  void SetWindow(wxWindow* window) { m_window = window; }
  void SetSizer(wxSizer* sizer) { m_sizer = sizer; }
  void DetachSizer() { m_sizer = nullptr; }
  int GetOption() const { return m_option; }
  int GetProportion() const { return m_option; }
  void SetOption(int option) { m_option = option; }
  void SetProportion(int option) { m_option = option; }
  int GetFlag() const { return m_flag; }
  void SetFlag(int flag) { m_flag = flag; }
  int GetBorder() const { return m_border; }
  void SetBorder(int border) { m_border = border; }
  wxObject* GetUserData() const { return m_userData; }
  wxSize GetSize() const { return m_size; }
  wxSize GetMinSize() const { return m_minSize; }
  void SetInitSize(int x, int y) { m_minSize = wxSize(x, y); }
  bool IsShown() const { return m_show; }
  void Show(bool show) { m_show = show; }

private:
  wxWindow* m_window = nullptr;
  wxSizer* m_sizer = nullptr;
  wxSize m_size;
  wxSize m_minSize;
  int m_option;
  int m_flag;
  int m_border;
  bool m_show = true;
  wxObject* m_userData;
};

class wxSizer : public wxObject
{
public:
  wxSizer() = default;
  wxSizer(const wxSizer&) = delete;
  wxSizer& operator=(const wxSizer&) = delete;
  ~wxSizer() override { Clear(false); }

  void Add(wxWindow* window, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    InsertItem(m_children.size(), new wxSizerItem(window, option, flag, border, userData));
  }
  void Add(wxSizer* sizer, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    InsertItem(m_children.size(), new wxSizerItem(sizer, option, flag, border, userData));
  }
  void Add(int width, int height, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    InsertItem(m_children.size(), new wxSizerItem(width, height, option, flag, border, userData));
  }
  void Insert(size_t before, wxWindow* window, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    InsertItem(before, new wxSizerItem(window, option, flag, border, userData));
  }
  void Insert(size_t before, wxSizer* sizer, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    InsertItem(before, new wxSizerItem(sizer, option, flag, border, userData));
  }
  void Insert(size_t before, int width, int height, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    InsertItem(before, new wxSizerItem(width, height, option, flag, border, userData));
  }
  void Prepend(wxWindow* window, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    Insert(0, window, option, flag, border, userData);
  }
  void Prepend(wxSizer* sizer, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    Insert(0, sizer, option, flag, border, userData);
  }
  void Prepend(int width, int height, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr)
  {
    Insert(0, width, height, option, flag, border, userData);
  }
  void AddSpacer(int size) { Add(size, size); }
  void AddStretchSpacer(int prop = 1) { Add(0, 0, prop); }

  // wx 2.4.2: removing a window detaches it, removing a sizer deletes it.
  bool Remove(wxWindow* window)
  {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
      if ((*it)->GetWindow() == window) {
        window->SetContainingSizer(nullptr);
        delete *it;
        m_children.erase(it);
        return true;
      }
    }
    return false;
  }
  bool Remove(wxSizer* sizer)
  {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
      if ((*it)->GetSizer() == sizer) {
        delete *it;
        m_children.erase(it);
        return true;
      }
    }
    return false;
  }
  bool Remove(int pos)
  {
    if (pos < 0 || static_cast<size_t>(pos) >= m_children.size()) {
      return false;
    }
    delete m_children[static_cast<size_t>(pos)];
    m_children.erase(m_children.begin() + pos);
    return true;
  }
  bool Detach(wxWindow* window) { return Remove(window); }
  bool Detach(wxSizer* sizer)
  {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
      if ((*it)->GetSizer() == sizer) {
        (*it)->DetachSizer();
        delete *it;
        m_children.erase(it);
        return true;
      }
    }
    return false;
  }
  void Clear(bool deleteWindows = false)
  {
    for (wxSizerItem* item : m_children) {
      if (item->GetWindow() != nullptr) {
        item->GetWindow()->SetContainingSizer(nullptr);
        if (deleteWindows) {
          item->GetWindow()->Destroy();
        }
      }
      delete item;
    }
    m_children.clear();
  }
  void DeleteWindows()
  {
    for (wxSizerItem* item : m_children) {
      if (item->GetWindow() != nullptr) {
        item->GetWindow()->Destroy();
        item->SetWindow(nullptr);
      }
    }
  }

  void SetMinSize(int width, int height) { m_minSize = wxSize(width, height); }
  void SetMinSize(const wxSize& size) { m_minSize = size; }
  bool SetItemMinSize(wxWindow* window, int width, int height)
  {
    (void)window;
    (void)width;
    (void)height;
    return true;
  }
  bool SetItemMinSize(wxSizer* sizer, int width, int height)
  {
    (void)sizer;
    (void)width;
    (void)height;
    return true;
  }
  bool SetItemMinSize(size_t pos, int width, int height)
  {
    (void)pos;
    (void)width;
    (void)height;
    return true;
  }
  wxSize GetSize() const { return m_size; }
  wxPoint GetPosition() const { return m_position; }
  wxSize GetMinSize() const { return m_minSize; }
  virtual void RecalcSizes() {}
  virtual wxSize CalcMin() { return m_minSize; }
  virtual void Layout() { RecalcSizes(); }
  wxSize Fit(wxWindow* window)
  {
    if (window != nullptr && m_minSize.x > 0 && m_minSize.y > 0) {
      window->SetClientSize(m_minSize);
    }
    return window != nullptr ? window->GetSize() : wxSize();
  }
  void FitInside(wxWindow* window) { (void)window; }
  void SetSizeHints(wxWindow* window)
  {
    if (window != nullptr) {
      window->SetSizeHints(m_minSize.x, m_minSize.y);
    }
  }
  void SetVirtualSizeHints(wxWindow* window) { (void)window; }
  void SetDimension(int x, int y, int width, int height)
  {
    m_position = wxPoint(x, y);
    m_size = wxSize(width, height);
    Layout();
  }
  const std::vector<wxSizerItem*>& GetChildren() const { return m_children; }
  void Show(wxWindow* window, bool show = true)
  {
    for (wxSizerItem* item : m_children) {
      if (item->GetWindow() == window) {
        item->Show(show);
      }
    }
  }
  void Show(wxSizer* sizer, bool show = true)
  {
    for (wxSizerItem* item : m_children) {
      if (item->GetSizer() == sizer) {
        item->Show(show);
      }
    }
  }
  void Hide(wxWindow* window) { Show(window, false); }
  void Hide(wxSizer* sizer) { Show(sizer, false); }

protected:
  void InsertItem(size_t before, wxSizerItem* item)
  {
    if (item->GetWindow() != nullptr) {
      item->GetWindow()->SetContainingSizer(this);
    }
    if (before > m_children.size()) {
      before = m_children.size();
    }
    m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(before), item);
  }

  std::vector<wxSizerItem*> m_children;
  wxSize m_size;
  wxSize m_minSize;
  wxPoint m_position;
};

inline wxSizerItem::~wxSizerItem()
{
  delete m_userData;
  delete m_sizer;
}

class wxBoxSizer : public wxSizer
{
public:
  explicit wxBoxSizer(int orient) : m_orient(orient) {}
  int GetOrientation() const { return m_orient; }

protected:
  int m_orient;
};

class wxStaticBoxSizer : public wxBoxSizer
{
public:
  wxStaticBoxSizer(wxStaticBox* box, int orient) : wxBoxSizer(orient), m_staticBox(box) {}
  wxStaticBox* GetStaticBox() const { return m_staticBox; }

protected:
  wxStaticBox* m_staticBox;
};

class wxGridSizer : public wxSizer
{
public:
  wxGridSizer(int rows, int cols, int vgap, int hgap) : m_rows(rows), m_cols(cols), m_vgap(vgap), m_hgap(hgap) {}
  wxGridSizer(int cols, int vgap = 0, int hgap = 0) : wxGridSizer(0, cols, vgap, hgap) {}
  void SetCols(int cols) { m_cols = cols; }
  void SetRows(int rows) { m_rows = rows; }
  void SetVGap(int gap) { m_vgap = gap; }
  void SetHGap(int gap) { m_hgap = gap; }
  int GetCols() const { return m_cols; }
  int GetRows() const { return m_rows; }
  int GetVGap() const { return m_vgap; }
  int GetHGap() const { return m_hgap; }

protected:
  int m_rows;
  int m_cols;
  int m_vgap;
  int m_hgap;
};

class wxFlexGridSizer : public wxGridSizer
{
public:
  wxFlexGridSizer(int rows, int cols, int vgap, int hgap) : wxGridSizer(rows, cols, vgap, hgap) {}
  wxFlexGridSizer(int cols, int vgap = 0, int hgap = 0) : wxGridSizer(cols, vgap, hgap) {}
  void AddGrowableRow(size_t idx) { (void)idx; }
  void RemoveGrowableRow(size_t idx) { (void)idx; }
  void AddGrowableCol(size_t idx) { (void)idx; }
  void RemoveGrowableCol(size_t idx) { (void)idx; }
};

class wxNotebookSizer : public wxSizer
{
public:
  explicit wxNotebookSizer(wxNotebook* notebook) : m_notebook(notebook) {}
  wxNotebook* GetNotebook() const { return m_notebook; }

protected:
  wxNotebook* m_notebook;
};

inline bool wxWindowBase::Layout()
{
  if (m_windowSizer != nullptr) {
    const wxSize client = GetClientSize();
    m_windowSizer->SetDimension(0, 0, client.x, client.y);
  }
  return true;
}

inline void wxWindowBase::SetSizer(wxSizer* sizer, bool deleteOld)
{
  if (sizer == m_windowSizer) {
    return;
  }
  if (deleteOld) {
    delete m_windowSizer;
  }
  m_windowSizer = sizer;
  SetAutoLayout(sizer != nullptr);
}

// wxLayoutConstraints (wx/layout.h): accepted and owned; constraint layout is not computed.
class wxLayoutConstraints : public wxObject
{};

inline void wxWindowBase::SetConstraints(wxLayoutConstraints* constraints)
{
  if (constraints != m_constraints) {
    delete m_constraints;
    m_constraints = constraints;
  }
}

inline wxWindowBase::~wxWindowBase()
{
  m_isBeingDeleted = true;
  wxPendingDelete.DeleteObject(this);
  wxTopLevelWindows.DeleteObject(reinterpret_cast<wxWindow*>(this));
  if (faf_wx_detail::FocusWindow() == reinterpret_cast<wxWindow*>(this)) {
    faf_wx_detail::FocusWindow() = nullptr;
  }
  if (faf_wx_detail::CaptureWindow() == reinterpret_cast<wxWindow*>(this)) {
    faf_wx_detail::CaptureWindow() = nullptr;
  }
  if (m_parent != nullptr) {
    m_parent->RemoveChild(this);
    m_parent = nullptr;
  }
  delete m_validator;
  delete m_toolTip;
  delete m_constraints;
  delete m_windowSizer;
}
