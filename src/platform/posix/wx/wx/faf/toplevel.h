#pragma once

// wx stand-in: top-level windows, menus and the standard dialogs (see wx/defs.h).
//
// Top-level windows are created hidden, as in wx, and are never displayed: showing one logs
// once. Modal dialogs return wxID_CANCEL without running a loop, message boxes print to
// stderr and return the negative answer.

#include "wx/faf/controls.h"

// --- Menus (wx/menu.h) -------------------------------------------------------------------------
class wxMenu;
class wxMenuBar;

class wxMenuItem : public wxObject
{
public:
  wxMenuItem(
    wxMenu* parentMenu = nullptr,
    int itemId = wxID_ANY,
    const wxString& text = wxEmptyString,
    const wxString& help = wxEmptyString,
    wxItemKind kind = wxITEM_NORMAL,
    wxMenu* subMenu = nullptr
  )
    : m_parentMenu(parentMenu), m_subMenu(subMenu), m_id(itemId), m_text(text), m_help(help), m_kind(kind)
  {}
  ~wxMenuItem() override;

  static wxMenuItem* New(
    wxMenu* parentMenu = nullptr,
    int itemId = wxID_ANY,
    const wxString& text = wxEmptyString,
    const wxString& help = wxEmptyString,
    wxItemKind kind = wxITEM_NORMAL,
    wxMenu* subMenu = nullptr
  )
  {
    return new wxMenuItem(parentMenu, itemId, text, help, kind, subMenu);
  }

  wxMenu* GetMenu() const { return m_parentMenu; }
  void SetMenu(wxMenu* menu) { m_parentMenu = menu; }
  int GetId() const { return m_id; }
  void SetId(int itemId) { m_id = itemId; }
  bool IsSeparator() const { return m_kind == wxITEM_SEPARATOR; }
  bool IsSubMenu() const { return m_subMenu != nullptr; }
  wxMenu* GetSubMenu() const { return m_subMenu; }
  void SetSubMenu(wxMenu* menu) { m_subMenu = menu; }
  const wxString& GetText() const { return m_text; }
  wxString GetLabel() const { return m_text; }
  void SetText(const wxString& text) { m_text = text; }
  const wxString& GetHelp() const { return m_help; }
  void SetHelp(const wxString& help) { m_help = help; }
  wxItemKind GetKind() const { return m_kind; }
  bool IsCheckable() const { return m_kind == wxITEM_CHECK || m_kind == wxITEM_RADIO; }
  void SetCheckable(bool checkable) { m_kind = checkable ? wxITEM_CHECK : wxITEM_NORMAL; }
  virtual void Enable(bool enable = true) { m_isEnabled = enable; }
  bool IsEnabled() const { return m_isEnabled; }
  virtual void Check(bool check = true) { m_isChecked = check; }
  bool IsChecked() const { return m_isChecked; }
  void Toggle() { Check(!m_isChecked); }
  void SetBitmap(const wxBitmap& bitmap) { m_bitmap = bitmap; }
  const wxBitmap& GetBitmap() const { return m_bitmap; }

private:
  wxMenu* m_parentMenu;
  wxMenu* m_subMenu;
  int m_id;
  wxString m_text;
  wxString m_help;
  wxItemKind m_kind;
  bool m_isEnabled = true;
  bool m_isChecked = false;
  wxBitmap m_bitmap;
};

class wxMenuBase : public wxEvtHandler
{
public:
  explicit wxMenuBase(const wxString& title = wxEmptyString, long style = 0) : m_title(title), m_style(style) {}
  ~wxMenuBase() override
  {
    for (wxMenuItem* item : m_items) {
      delete item;
    }
  }

  wxMenuItem* Append(int itemId, const wxString& text, const wxString& help = wxEmptyString, wxItemKind kind = wxITEM_NORMAL)
  {
    return DoAppend(wxMenuItem::New(AsMenu(), itemId, text, help, kind));
  }
  void Append(int itemId, const wxString& text, const wxString& help, bool isCheckable)
  {
    Append(itemId, text, help, isCheckable ? wxITEM_CHECK : wxITEM_NORMAL);
  }
  wxMenuItem* Append(int itemId, const wxString& text, wxMenu* subMenu, const wxString& help = wxEmptyString)
  {
    return DoAppend(wxMenuItem::New(AsMenu(), itemId, text, help, wxITEM_NORMAL, subMenu));
  }
  wxMenuItem* Append(wxMenuItem* item) { return DoAppend(item); }
  wxMenuItem* AppendSeparator() { return Append(wxMenuItem::New(AsMenu(), wxID_ANY, wxEmptyString, wxEmptyString, wxITEM_SEPARATOR)); }
  wxMenuItem* AppendCheckItem(int itemId, const wxString& text, const wxString& help = wxEmptyString)
  {
    return Append(itemId, text, help, wxITEM_CHECK);
  }
  wxMenuItem* AppendRadioItem(int itemId, const wxString& text, const wxString& help = wxEmptyString)
  {
    return Append(itemId, text, help, wxITEM_RADIO);
  }
  wxMenuItem* Insert(size_t pos, wxMenuItem* item)
  {
    m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(std::min(pos, m_items.size())), item);
    return item;
  }
  wxMenuItem* Insert(size_t pos, int itemId, const wxString& text, const wxString& help = wxEmptyString, wxItemKind kind = wxITEM_NORMAL)
  {
    return Insert(pos, wxMenuItem::New(AsMenu(), itemId, text, help, kind));
  }
  wxMenuItem* Prepend(int itemId, const wxString& text, const wxString& help = wxEmptyString, wxItemKind kind = wxITEM_NORMAL)
  {
    return Insert(0, itemId, text, help, kind);
  }
  wxMenuItem* Remove(int itemId)
  {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
      if ((*it)->GetId() == itemId) {
        wxMenuItem* const item = *it;
        m_items.erase(it);
        return item;
      }
    }
    return nullptr;
  }
  wxMenuItem* Remove(wxMenuItem* item)
  {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
      if (*it == item) {
        m_items.erase(it);
        return item;
      }
    }
    return nullptr;
  }
  bool Delete(int itemId)
  {
    wxMenuItem* const item = Remove(itemId);
    if (item == nullptr) {
      return false;
    }
    item->SetSubMenu(nullptr);
    delete item;
    return true;
  }
  bool Destroy(int itemId)
  {
    wxMenuItem* const item = Remove(itemId);
    delete item;
    return item != nullptr;
  }

  size_t GetMenuItemCount() const { return m_items.size(); }
  const std::vector<wxMenuItem*>& GetMenuItems() const { return m_items; }
  wxMenuItem* FindItem(int itemId, wxMenu** menu = nullptr) const;
  int FindItem(const wxString& item) const
  {
    for (wxMenuItem* candidate : m_items) {
      if (candidate->GetText() == item) {
        return candidate->GetId();
      }
    }
    return wxID_ANY;
  }
  wxMenuItem* FindItemByPosition(size_t position) const
  {
    return position < m_items.size() ? m_items[position] : nullptr;
  }
  void Enable(int itemId, bool enable)
  {
    if (wxMenuItem* const item = FindItem(itemId)) {
      item->Enable(enable);
    }
  }
  bool IsEnabled(int itemId) const
  {
    const wxMenuItem* const item = FindItem(itemId);
    return item != nullptr && item->IsEnabled();
  }
  void Check(int itemId, bool check)
  {
    if (wxMenuItem* const item = FindItem(itemId)) {
      item->Check(check);
    }
  }
  bool IsChecked(int itemId) const
  {
    const wxMenuItem* const item = FindItem(itemId);
    return item != nullptr && item->IsChecked();
  }
  void SetLabel(int itemId, const wxString& label)
  {
    if (wxMenuItem* const item = FindItem(itemId)) {
      item->SetText(label);
    }
  }
  wxString GetLabel(int itemId) const
  {
    const wxMenuItem* const item = FindItem(itemId);
    return item != nullptr ? item->GetText() : wxString();
  }
  void SetHelpString(int itemId, const wxString& help)
  {
    if (wxMenuItem* const item = FindItem(itemId)) {
      item->SetHelp(help);
    }
  }
  wxString GetHelpString(int itemId) const
  {
    const wxMenuItem* const item = FindItem(itemId);
    return item != nullptr ? item->GetHelp() : wxString();
  }
  void SetTitle(const wxString& title) { m_title = title; }
  const wxString& GetTitle() const { return m_title; }
  void SetEventHandler(wxEvtHandler* handler) { m_eventHandler = handler; }
  wxEvtHandler* GetEventHandler() const { return m_eventHandler; }
  void SetInvokingWindow(wxWindow* window) { m_invokingWindow = window; }
  wxWindow* GetInvokingWindow() const { return m_invokingWindow; }
  long GetStyle() const { return m_style; }
  void UpdateUI(wxEvtHandler* source = nullptr) { (void)source; }
  wxMenuBar* GetMenuBar() const { return m_menuBar; }
  virtual void Attach(wxMenuBar* menubar) { m_menuBar = menubar; }
  virtual void Detach() { m_menuBar = nullptr; }
  bool IsAttached() const { return m_menuBar != nullptr; }
  void SetParent(wxMenu* parent) { m_menuParent = parent; }
  wxMenu* GetParent() const { return m_menuParent; }

protected:
  wxMenu* AsMenu();
  wxMenuItem* DoAppend(wxMenuItem* item)
  {
    m_items.push_back(item);
    return item;
  }

  std::vector<wxMenuItem*> m_items;
  wxString m_title;
  long m_style;
  wxEvtHandler* m_eventHandler = nullptr;
  wxWindow* m_invokingWindow = nullptr;
  wxMenuBar* m_menuBar = nullptr;
  wxMenu* m_menuParent = nullptr;
};

class wxMenu : public wxMenuBase
{
public:
  explicit wxMenu(const wxString& title = wxEmptyString, long style = 0) : wxMenuBase(title, style) {}
  explicit wxMenu(long style) : wxMenuBase(wxEmptyString, style) {}
  void Break() {}
};

inline wxMenu* wxMenuBase::AsMenu()
{
  return static_cast<wxMenu*>(this);
}

inline wxMenuItem::~wxMenuItem()
{
  delete m_subMenu;
}

inline wxMenuItem* wxMenuBase::FindItem(int itemId, wxMenu** menu) const
{
  for (wxMenuItem* item : m_items) {
    if (item->GetId() == itemId) {
      if (menu != nullptr) {
        *menu = const_cast<wxMenuBase*>(this)->AsMenu();
      }
      return item;
    }
    if (item->GetSubMenu() != nullptr) {
      if (wxMenuItem* const found = item->GetSubMenu()->FindItem(itemId, menu)) {
        return found;
      }
    }
  }
  return nullptr;
}

class wxMenuBarBase : public wxWindow
{
public:
  wxMenuBarBase() = default;
  ~wxMenuBarBase() override
  {
    for (wxMenu* menu : m_menus) {
      delete menu;
    }
  }

  virtual bool Append(wxMenu* menu, const wxString& title)
  {
    return Insert(m_menus.size(), menu, title);
  }
  virtual bool Insert(size_t pos, wxMenu* menu, const wxString& title)
  {
    if (menu == nullptr) {
      return false;
    }
    pos = std::min(pos, m_menus.size());
    m_menus.insert(m_menus.begin() + static_cast<std::ptrdiff_t>(pos), menu);
    m_titles.insert(m_titles.begin() + static_cast<std::ptrdiff_t>(pos), title);
    menu->Attach(reinterpret_cast<wxMenuBar*>(this));
    menu->SetTitle(title);
    return true;
  }
  size_t GetMenuCount() const { return m_menus.size(); }
  wxMenu* GetMenu(size_t pos) const { return pos < m_menus.size() ? m_menus[pos] : nullptr; }
  virtual wxMenu* Replace(size_t pos, wxMenu* menu, const wxString& title)
  {
    if (pos >= m_menus.size()) {
      return nullptr;
    }
    wxMenu* const old = m_menus[pos];
    m_menus[pos] = menu;
    m_titles[pos] = title;
    old->Detach();
    menu->Attach(reinterpret_cast<wxMenuBar*>(this));
    return old;
  }
  virtual wxMenu* Remove(size_t pos)
  {
    if (pos >= m_menus.size()) {
      return nullptr;
    }
    wxMenu* const menu = m_menus[pos];
    m_menus.erase(m_menus.begin() + static_cast<std::ptrdiff_t>(pos));
    m_titles.erase(m_titles.begin() + static_cast<std::ptrdiff_t>(pos));
    menu->Detach();
    return menu;
  }
  virtual void EnableTop(size_t pos, bool enable)
  {
    (void)pos;
    (void)enable;
  }
  int FindMenu(const wxString& title) const
  {
    for (size_t i = 0; i < m_titles.size(); ++i) {
      if (m_titles[i] == title) {
        return static_cast<int>(i);
      }
    }
    return wxID_ANY;
  }
  int FindMenuItem(const wxString& menuString, const wxString& itemString) const
  {
    const int menu = FindMenu(menuString);
    return menu == wxID_ANY ? wxID_ANY : m_menus[static_cast<size_t>(menu)]->FindItem(itemString);
  }
  wxMenuItem* FindItem(int itemId, wxMenu** menu = nullptr) const
  {
    for (wxMenu* candidate : m_menus) {
      if (wxMenuItem* const item = candidate->FindItem(itemId, menu)) {
        return item;
      }
    }
    return nullptr;
  }
  void Enable(int itemId, bool enable)
  {
    if (wxMenuItem* const item = FindItem(itemId)) {
      item->Enable(enable);
    }
  }
  void Check(int itemId, bool check)
  {
    if (wxMenuItem* const item = FindItem(itemId)) {
      item->Check(check);
    }
  }
  bool IsChecked(int itemId) const
  {
    const wxMenuItem* const item = FindItem(itemId);
    return item != nullptr && item->IsChecked();
  }
  bool IsEnabled(int itemId) const
  {
    const wxMenuItem* const item = FindItem(itemId);
    return item != nullptr && item->IsEnabled();
  }
  void SetLabel(int itemId, const wxString& label)
  {
    if (wxMenuItem* const item = FindItem(itemId)) {
      item->SetText(label);
    }
  }
  wxString GetLabel(int itemId) const
  {
    const wxMenuItem* const item = FindItem(itemId);
    return item != nullptr ? item->GetText() : wxString();
  }
  void SetLabelTop(size_t pos, const wxString& label)
  {
    if (pos < m_titles.size()) {
      m_titles[pos] = label;
    }
  }
  wxString GetLabelTop(size_t pos) const { return pos < m_titles.size() ? m_titles[pos] : wxString(); }
  wxFrame* GetFrame() const { return m_menuBarFrame; }
  bool IsAttached() const { return m_menuBarFrame != nullptr; }
  virtual void Attach(wxFrame* frame) { m_menuBarFrame = frame; }
  virtual void Detach() { m_menuBarFrame = nullptr; }

protected:
  std::vector<wxMenu*> m_menus;
  std::vector<wxString> m_titles;
  wxFrame* m_menuBarFrame = nullptr;
};

class wxMenuBar : public wxMenuBarBase
{
public:
  explicit wxMenuBar(long style = 0) { m_windowStyle = style; }
  wxMenuBar(int n, wxMenu* menus[], const wxString titles[])
  {
    for (int i = 0; i < n; ++i) {
      Append(menus[i], titles[i]);
    }
  }
};

// --- Top-level windows (wx/toplevel.h) ---------------------------------------------------------
#define wxFULLSCREEN_NOMENUBAR 0x0001
#define wxFULLSCREEN_NOTOOLBAR 0x0002
#define wxFULLSCREEN_NOSTATUSBAR 0x0004
#define wxFULLSCREEN_NOBORDER 0x0008
#define wxFULLSCREEN_NOCAPTION 0x0010
#define wxFULLSCREEN_ALL \
  (wxFULLSCREEN_NOMENUBAR | wxFULLSCREEN_NOTOOLBAR | wxFULLSCREEN_NOSTATUSBAR | wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION)
#define wxUSER_ATTENTION_INFO 1
#define wxUSER_ATTENTION_ERROR 2

class wxTopLevelWindowBase : public wxWindow
{
public:
  wxTopLevelWindowBase() { m_isShown = false; }
  ~wxTopLevelWindowBase() override
  {
    wxTopLevelWindows.DeleteObject(static_cast<wxWindow*>(this));
  }

  bool IsTopLevel() const override { return true; }

  // wx destroys a top-level window on the next idle (wxPendingDelete), not at once.
  bool Destroy() override
  {
    if (!wxPendingDelete.Member(this)) {
      wxPendingDelete.Append(this);
    }
    Hide();
    return true;
  }

  // wxTopLevelWindowMSW::Show: the window shows maximized when Maximize() was asked for
  // while it was hidden, comes to the front, and gets a wxEVT_ACTIVATE.
  bool Show(bool show = true) override
  {
    if (GetHandle() == 0) {
      if (show) {
        FAF_POSIX_STUB("wx top-level window shown: no window system (headless), nothing is displayed");
      }
      return wxWindow::Show(show);
    }
    if (show == m_isShown) {
      return false;
    }
    m_isShown = show;
    const HWND hwnd = reinterpret_cast<HWND>(GetHandle());
    if (show) {
      ::ShowWindow(hwnd, m_maximizeOnShow ? SW_MAXIMIZE : SW_SHOW);
      m_maximizeOnShow = false;
      ::BringWindowToTop(hwnd);
      wxActivateEvent event(wxEVT_ACTIVATE, true, m_windowId);
      event.SetEventObject(this);
      GetEventHandler()->ProcessEvent(event);
    } else {
      ::ShowWindow(hwnd, SW_HIDE);
    }
    return true;
  }

  virtual void Maximize(bool maximize = true)
  {
    if (GetHandle() == 0) {
      m_maximized = maximize;
      return;
    }
    if (IsShown()) {
      ::ShowWindow(reinterpret_cast<HWND>(GetHandle()), maximize ? SW_MAXIMIZE : SW_RESTORE);
    } else {
      // A hidden window cannot be maximized without showing it: done by Show().
      m_maximizeOnShow = maximize;
    }
  }
  virtual bool IsMaximized() const
  {
    if (GetHandle() == 0) {
      return m_maximized;
    }
    return ::IsZoomed(reinterpret_cast<HWND>(GetHandle())) != FALSE || m_maximizeOnShow;
  }
  virtual void Iconize(bool iconize = true)
  {
    if (GetHandle() == 0) {
      m_iconized = iconize;
      return;
    }
    ::ShowWindow(reinterpret_cast<HWND>(GetHandle()), iconize ? SW_MINIMIZE : SW_RESTORE);
  }
  virtual bool IsIconized() const
  {
    if (GetHandle() == 0) {
      return m_iconized;
    }
    return ::IsIconic(reinterpret_cast<HWND>(GetHandle())) != FALSE;
  }
  virtual void Restore()
  {
    if (GetHandle() != 0) {
      ::ShowWindow(reinterpret_cast<HWND>(GetHandle()), SW_RESTORE);
      return;
    }
    m_maximized = false;
    m_iconized = false;
  }
  void SetTitle(const wxString& title) override
  {
    wxWindow::SetTitle(title);
    if (GetHandle() != 0) {
      ::SetWindowTextW(reinterpret_cast<HWND>(GetHandle()), title.c_str());
    }
  }
  virtual void SetIcon(const wxIcon& icon) { m_icon = icon; }
  const wxIcon& GetIcon() const { return m_icon; }
  virtual bool ShowFullScreen(bool show, long style = wxFULLSCREEN_ALL)
  {
    (void)style;
    m_fullScreen = show;
    return true;
  }
  virtual bool IsFullScreen() const { return m_fullScreen; }
  virtual bool IsActive()
  {
    if (GetHandle() != 0) {
      return ::GetActiveWindow() == reinterpret_cast<HWND>(GetHandle());
    }
    return wxWindow::FindFocus() != nullptr && wxGetTopLevelParent(wxWindow::FindFocus()) == this;
  }
  virtual void RequestUserAttention(int flags = wxUSER_ATTENTION_INFO) { (void)flags; }
  wxWindow* GetDefaultItem() const { return m_winDefault; }
  wxWindow* SetDefaultItem(wxWindow* window)
  {
    wxWindow* const old = m_winDefault;
    m_winDefault = window;
    return old;
  }

  // wxTopLevelWindowBase::OnSize: lay the client area out through the sizer, or give the
  // single child window (tool and status bars aside) the whole client area.
  void OnSize(wxSizeEvent& event)
  {
    (void)event;
    DoLayout();
  }
  void DoLayout()
  {
    if (GetSizer() != nullptr || GetAutoLayout()) {
      Layout();
      return;
    }
    wxWindow* only = nullptr;
    for (wxWindow* child : GetChildren()) {
      if (child->IsTopLevel() || IsBarWindow(child)) {
        continue;
      }
      if (only != nullptr) {
        return;
      }
      only = child;
    }
    if (only != nullptr) {
      const wxSize client = GetClientSize();
      only->SetSize(0, 0, client.x, client.y);
    }
  }

protected:
  virtual bool IsBarWindow(const wxWindow* window) const
  {
    (void)window;
    return false;
  }

  // wxTopLevelWindowBase's EVT_SIZE row: the client area is laid out when nothing else
  // handled the size event.
  bool FafDefaultOnSize(wxSizeEvent& event) override
  {
    OnSize(event);
    return true;
  }

  bool FafHandleUnprocessedClose(wxCloseEvent& event) override
  {
    (void)event;
    Destroy();
    return true;
  }
  bool CreateTopLevel(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& title,
    const wxPoint& pos,
    const wxSize& size,
    long style,
    const wxString& name
  )
  {
    CreateBase(parent, winid, pos, size, style, name, 400, 250);
    m_title = title;
    wxTopLevelWindows.Append(static_cast<wxWindow*>(this));
    FafCreateNative(pos, size, title);
    return true;
  }

  wxIcon m_icon;
  wxWindow* m_winDefault = nullptr;
  bool m_maximized = false;
  bool m_iconized = false;
  bool m_fullScreen = false;
  bool m_maximizeOnShow = false;
};

class wxTopLevelWindowMSW : public wxTopLevelWindowBase
{
public:
  wxTopLevelWindowMSW() = default;
};

class wxTopLevelWindow : public wxTopLevelWindowMSW
{
public:
  wxTopLevelWindow() = default;
  wxTopLevelWindow(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_FRAME_STYLE,
    const wxString& name = wxFrameNameStr
  )
  {
    Create(parent, winid, title, pos, size, style, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_FRAME_STYLE,
    const wxString& name = wxFrameNameStr
  )
  {
    return CreateTopLevel(parent, winid, title, pos, size, style, name);
  }
};

class wxFrameBase : public wxTopLevelWindow
{
public:
  wxFrameBase() = default;
  ~wxFrameBase() override
  {
    // DeleteAllBars: the frame owns its menu bar; the tool and status bars are children.
    delete m_frameMenuBar;
    m_frameMenuBar = nullptr;
  }

  virtual void SetMenuBar(wxMenuBar* menubar)
  {
    if (menubar == m_frameMenuBar) {
      return;
    }
    if (m_frameMenuBar != nullptr) {
      m_frameMenuBar->Detach();
    }
    m_frameMenuBar = menubar;
    if (menubar != nullptr) {
      menubar->Attach(reinterpret_cast<wxFrame*>(this));
    }
  }
  virtual wxMenuBar* GetMenuBar() const { return m_frameMenuBar; }
  virtual wxStatusBar* CreateStatusBar(
    int number = 1,
    long style = wxST_SIZEGRIP,
    wxWindowID winid = 0,
    const wxString& name = wxStatusLineNameStr
  )
  {
    delete m_frameStatusBar;
    m_frameStatusBar = new wxStatusBar(this, winid, style, name);
    m_frameStatusBar->SetFieldsCount(number);
    return m_frameStatusBar;
  }
  virtual wxStatusBar* GetStatusBar() const { return m_frameStatusBar; }
  virtual void SetStatusBar(wxStatusBar* statBar) { m_frameStatusBar = statBar; }
  virtual void SetStatusText(const wxString& text, int number = 0)
  {
    if (m_frameStatusBar != nullptr) {
      m_frameStatusBar->SetStatusText(text, number);
    }
  }
  virtual void SetStatusWidths(int n, const int widths[])
  {
    if (m_frameStatusBar != nullptr) {
      m_frameStatusBar->SetStatusWidths(n, widths);
    }
  }
  virtual wxToolBar* CreateToolBar(long style = -1, wxWindowID winid = -1, const wxString& name = wxToolBarNameStr)
  {
    if (style == -1) {
      style = wxNO_BORDER | wxTB_HORIZONTAL | wxTB_FLAT;
    }
    delete m_frameToolBar;
    m_frameToolBar = new wxToolBar(this, winid, wxDefaultPosition, wxDefaultSize, style, name);
    return m_frameToolBar;
  }
  virtual wxToolBar* GetToolBar() const { return m_frameToolBar; }
  virtual void SetToolBar(wxToolBar* toolbar) { m_frameToolBar = toolbar; }
  bool ProcessCommand(int winid)
  {
    wxCommandEvent event(wxEVT_COMMAND_MENU_SELECTED, winid);
    event.SetEventObject(this);
    return GetEventHandler()->ProcessEvent(event);
  }
  void Command(int winid) { ProcessCommand(winid); }

  bool IsBarWindow(const wxWindow* window) const override
  {
    return window == m_frameStatusBar || window == m_frameToolBar;
  }

  void RemoveChild(wxWindowBase* child) override
  {
    if (child == m_frameStatusBar) {
      m_frameStatusBar = nullptr;
    }
    if (child == m_frameToolBar) {
      m_frameToolBar = nullptr;
    }
    wxTopLevelWindow::RemoveChild(child);
  }

protected:
  wxMenuBar* m_frameMenuBar = nullptr;
  wxStatusBar* m_frameStatusBar = nullptr;
  wxToolBar* m_frameToolBar = nullptr;
};

class wxFrame : public wxFrameBase
{
public:
  wxFrame() = default;
  wxFrame(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_FRAME_STYLE,
    const wxString& name = wxFrameNameStr
  )
  {
    Create(parent, winid, title, pos, size, style, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_FRAME_STYLE,
    const wxString& name = wxFrameNameStr
  )
  {
    return CreateTopLevel(parent, winid, title, pos, size, style, name);
  }
};

class wxMiniFrame : public wxFrame
{
public:
  using wxFrame::wxFrame;
};

class wxDialogBase : public wxTopLevelWindow
{
public:
  wxDialogBase() = default;
  void SetReturnCode(int returnCode) { m_returnCode = returnCode; }
  int GetReturnCode() const { return m_returnCode; }
  wxSizer* CreateButtonSizer(long flags);
  wxSizer* CreateTextSizer(const wxString& message)
  {
    wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(reinterpret_cast<wxWindow*>(this), wxID_ANY, message));
    return sizer;
  }

protected:
  int m_returnCode = 0;
};

class wxDialog : public wxDialogBase
{
public:
  wxDialog() = default;
  wxDialog(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_DIALOG_STYLE,
    const wxString& name = wxDialogNameStr
  )
  {
    Create(parent, winid, title, pos, size, style, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_DIALOG_STYLE,
    const wxString& name = wxDialogNameStr
  )
  {
    return CreateTopLevel(parent, winid, title, pos, size, style, name);
  }

  // No modal loop runs: the dialog is reported as cancelled.
  virtual int ShowModal()
  {
    FAF_POSIX_STUB("wxDialog::ShowModal: no dialogs in the wx stand-in, returning wxID_CANCEL");
    SetReturnCode(wxID_CANCEL);
    return wxID_CANCEL;
  }
  virtual void EndModal(int retCode)
  {
    SetReturnCode(retCode);
    m_isModal = false;
    Hide();
  }
  virtual bool IsModal() const { return m_isModal; }
  void SetModal(bool modal) { m_isModal = modal; }

  // The library's standard button handlers (wxDialog's event table rows for wxID_OK,
  // wxID_APPLY and wxID_CANCEL).
  void OnOK(wxCommandEvent& event)
  {
    (void)event;
    if (Validate() && TransferDataFromWindow()) {
      if (IsModal()) {
        EndModal(wxID_OK);
      } else {
        SetReturnCode(wxID_OK);
        Show(false);
      }
    }
  }
  void OnApply(wxCommandEvent& event)
  {
    (void)event;
    if (Validate()) {
      TransferDataFromWindow();
    }
  }
  void OnCancel(wxCommandEvent& event)
  {
    (void)event;
    if (IsModal()) {
      EndModal(wxID_CANCEL);
    } else {
      SetReturnCode(wxID_CANCEL);
      Show(false);
    }
  }
  void OnCloseWindow(wxCloseEvent& event)
  {
    (void)event;
    wxCommandEvent cancel(wxEVT_COMMAND_BUTTON_CLICKED, wxID_CANCEL);
    OnCancel(cancel);
  }

protected:
  bool FafHandleUnprocessedClose(wxCloseEvent& event) override
  {
    OnCloseWindow(event);
    return true;
  }
  bool TryParent(wxEvent& event) override
  {
    // wxDialog's table answers the standard buttons when nothing else does.
    if (event.GetEventType() == wxEVT_COMMAND_BUTTON_CLICKED) {
      wxCommandEvent& command = static_cast<wxCommandEvent&>(event);
      switch (event.GetId()) {
        case wxID_OK:
          OnOK(command);
          return true;
        case wxID_APPLY:
          OnApply(command);
          return true;
        case wxID_CANCEL:
          OnCancel(command);
          return true;
        default:
          break;
      }
    }
    return wxDialogBase::TryParent(event);
  }

  bool m_isModal = false;
};

inline wxSizer* wxDialogBase::CreateButtonSizer(long flags)
{
  wxBoxSizer* const sizer = new wxBoxSizer(wxHORIZONTAL);
  wxWindow* const self = static_cast<wxDialog*>(this);
  if ((flags & wxOK) != 0) {
    sizer->Add(new wxButton(self, wxID_OK, L"OK"));
  }
  if ((flags & wxYES_NO) != 0) {
    sizer->Add(new wxButton(self, wxID_YES, L"Yes"));
    sizer->Add(new wxButton(self, wxID_NO, L"No"));
  }
  if ((flags & wxCANCEL) != 0) {
    sizer->Add(new wxButton(self, wxID_CANCEL, L"Cancel"));
  }
  return sizer;
}

// --- Standard dialogs --------------------------------------------------------------------------
class wxMessageDialog : public wxDialog
{
public:
  wxMessageDialog(
    wxWindow* parent,
    const wxString& message,
    const wxString& caption = wxMessageBoxCaptionStr,
    long style = wxOK | wxCENTRE,
    const wxPoint& pos = wxDefaultPosition
  )
    : m_message(message), m_caption(caption), m_dialogStyle(style)
  {
    (void)parent;
    (void)pos;
  }

  // Prints the message; answers No / Cancel where the box offers them, OK otherwise.
  int ShowModal() override
  {
    const wxCharBuffer caption = m_caption.mb_str(wxConvUTF8);
    const wxCharBuffer message = m_message.mb_str(wxConvUTF8);
    std::fprintf(
      stderr, "[wx message box] %s: %s\n", caption.data() != nullptr ? caption.data() : "",
      message.data() != nullptr ? message.data() : ""
    );
    if ((m_dialogStyle & wxYES_NO) == wxYES_NO) {
      return wxID_NO;
    }
    if ((m_dialogStyle & wxCANCEL) != 0) {
      return wxID_CANCEL;
    }
    return wxID_OK;
  }

private:
  wxString m_message;
  wxString m_caption;
  long m_dialogStyle;
};

inline int wxMessageBox(
  const wxString& message,
  const wxString& caption = wxMessageBoxCaptionStr,
  long style = wxOK | wxCENTRE,
  wxWindow* parent = nullptr,
  int x = -1,
  int y = -1
)
{
  (void)x;
  (void)y;
  wxMessageDialog dialog(parent, message, caption, style);
  switch (dialog.ShowModal()) {
    case wxID_YES:
      return wxYES;
    case wxID_NO:
      return wxNO;
    case wxID_CANCEL:
      return wxCANCEL;
    default:
      return wxOK;
  }
}

class wxFileDialog : public wxDialog
{
public:
  wxFileDialog(
    wxWindow* parent,
    const wxString& message = wxFileSelectorPromptStr,
    const wxString& defaultDir = wxEmptyString,
    const wxString& defaultFile = wxEmptyString,
    const wxString& wildCard = wxFileSelectorDefaultWildcardStr,
    long style = 0,
    const wxPoint& pos = wxDefaultPosition
  )
    : m_message(message), m_dir(defaultDir), m_fileName(defaultFile), m_wildCard(wildCard), m_dialogStyle(style)
  {
    (void)parent;
    (void)pos;
  }
  void SetMessage(const wxString& message) { m_message = message; }
  void SetPath(const wxString& path) { m_path = path; }
  void SetDirectory(const wxString& dir) { m_dir = dir; }
  void SetFilename(const wxString& name) { m_fileName = name; }
  void SetWildcard(const wxString& wildCard) { m_wildCard = wildCard; }
  void SetStyle(long style) { m_dialogStyle = style; }
  void SetFilterIndex(int filterIndex) { m_filterIndex = filterIndex; }
  wxString GetMessage() const { return m_message; }
  wxString GetPath() const { return m_path; }
  void GetPaths(wxArrayString& paths) const
  {
    paths.Clear();
    if (!m_path.IsEmpty()) {
      paths.Add(m_path);
    }
  }
  wxString GetDirectory() const { return m_dir; }
  wxString GetFilename() const { return m_fileName; }
  void GetFilenames(wxArrayString& files) const
  {
    files.Clear();
    if (!m_fileName.IsEmpty()) {
      files.Add(m_fileName);
    }
  }
  wxString GetWildcard() const { return m_wildCard; }
  long GetStyle() const { return m_dialogStyle; }
  int GetFilterIndex() const { return m_filterIndex; }

private:
  wxString m_message;
  wxString m_dir;
  wxString m_fileName;
  wxString m_wildCard;
  wxString m_path;
  long m_dialogStyle;
  int m_filterIndex = 0;
};

class wxDirDialog : public wxDialog
{
public:
  wxDirDialog(
    wxWindow* parent,
    const wxString& message = wxDirSelectorPromptStr,
    const wxString& defaultPath = wxEmptyString,
    long style = 0,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    const wxString& name = wxDirDialogNameStr
  )
    : m_message(message), m_path(defaultPath), m_dialogStyle(style)
  {
    (void)parent;
    (void)pos;
    (void)size;
    (void)name;
  }
  void SetMessage(const wxString& message) { m_message = message; }
  void SetPath(const wxString& path) { m_path = path; }
  void SetStyle(long style) { m_dialogStyle = style; }
  wxString GetMessage() const { return m_message; }
  wxString GetPath() const { return m_path; }
  long GetStyle() const { return m_dialogStyle; }

private:
  wxString m_message;
  wxString m_path;
  long m_dialogStyle;
};

inline wxString wxDirSelector(
  const wxString& message = wxDirSelectorPromptStr,
  const wxString& defaultPath = wxEmptyString,
  long style = 0,
  const wxPoint& pos = wxDefaultPosition,
  wxWindow* parent = nullptr
)
{
  wxDirDialog dialog(parent, message, defaultPath, style, pos);
  return dialog.ShowModal() == wxID_OK ? dialog.GetPath() : wxString();
}

inline wxString wxFileSelector(
  const wxString& message = wxFileSelectorPromptStr,
  const wxString& defaultPath = wxEmptyString,
  const wxString& defaultFile = wxEmptyString,
  const wxString& defaultExtension = wxEmptyString,
  const wxString& wildcard = wxFileSelectorDefaultWildcardStr,
  int flags = 0,
  wxWindow* parent = nullptr,
  int x = -1,
  int y = -1
)
{
  (void)defaultExtension;
  (void)x;
  (void)y;
  wxFileDialog dialog(parent, message, defaultPath, defaultFile, wildcard, flags);
  return dialog.ShowModal() == wxID_OK ? dialog.GetPath() : wxString();
}

inline wxString wxGetTextFromUser(
  const wxString& message,
  const wxString& caption = L"Input text",
  const wxString& defaultValue = wxEmptyString,
  wxWindow* parent = nullptr,
  int x = -1,
  int y = -1,
  bool centre = true
)
{
  (void)message;
  (void)caption;
  (void)defaultValue;
  (void)parent;
  (void)x;
  (void)y;
  (void)centre;
  FAF_POSIX_STUB("wxGetTextFromUser: no dialogs in the wx stand-in");
  return wxString();
}

// --- Splash screen (wx/splash.h) ---------------------------------------------------------------
class wxSplashScreenWindow : public wxWindow
{
public:
  wxSplashScreenWindow(const wxBitmap& bitmap, wxWindow* parent, wxWindowID winid) : m_bitmap(bitmap)
  {
    Create(parent, winid, wxDefaultPosition, wxSize(bitmap.GetWidth(), bitmap.GetHeight()), wxNO_BORDER);
  }
  void SetBitmap(const wxBitmap& bitmap) { m_bitmap = bitmap; }
  wxBitmap& GetBitmap() { return m_bitmap; }

private:
  wxBitmap m_bitmap;
};

class wxSplashScreen : public wxFrame
{
public:
  wxSplashScreen(
    const wxBitmap& bitmap,
    long splashStyle,
    int milliseconds,
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
  )
    : wxFrame(parent, winid, wxEmptyString, pos, size, style), m_splashStyle(splashStyle), m_milliseconds(milliseconds)
  {
    m_window = new wxSplashScreenWindow(bitmap, this, wxID_ANY);
    Show(true);
  }
  long GetSplashStyle() const { return m_splashStyle; }
  wxSplashScreenWindow* GetSplashWindow() const { return m_window; }
  int GetTimeout() const { return m_milliseconds; }

private:
  wxSplashScreenWindow* m_window = nullptr;
  long m_splashStyle;
  int m_milliseconds;
};

// --- Global helpers ----------------------------------------------------------------------------
inline wxPoint wxGetMousePosition()
{
  if (faf_wx_detail::NativeWindows()) {
    POINT pt{};
    ::GetCursorPos(&pt);
    return wxPoint(pt.x, pt.y);
  }
  return wxPoint(0, 0);
}

inline void wxGetMousePosition(int* x, int* y)
{
  const wxPoint pt = wxGetMousePosition();
  if (x != nullptr) {
    *x = pt.x;
  }
  if (y != nullptr) {
    *y = pt.y;
  }
}

inline void wxDisplaySize(int* width, int* height)
{
  if (width != nullptr) {
    *width = wxSystemSettings::GetMetric(wxSYS_SCREEN_X);
  }
  if (height != nullptr) {
    *height = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);
  }
}

inline wxSize wxGetDisplaySize()
{
  int width = 0;
  int height = 0;
  wxDisplaySize(&width, &height);
  return wxSize(width, height);
}

inline wxWindow* wxFindWindowAtPoint(const wxPoint& pt)
{
  if (faf_wx_detail::NativeWindows()) {
    for (HWND hwnd = ::WindowFromPoint(POINT{pt.x, pt.y}); hwnd != nullptr; hwnd = ::GetParent(hwnd)) {
      if (wxWindow* const window = wxFindWinFromHandle(reinterpret_cast<WXHWND>(hwnd))) {
        return window;
      }
    }
  }
  return nullptr;
}

inline void wxBell() {}
inline void wxBeginBusyCursor(wxCursor* cursor = wxHOURGLASS_CURSOR)
{
  (void)cursor;
}
inline void wxEndBusyCursor() {}
inline bool wxIsBusy()
{
  return false;
}

class wxBusyCursor
{
public:
  explicit wxBusyCursor(wxCursor* cursor = wxHOURGLASS_CURSOR) { wxBeginBusyCursor(cursor); }
  ~wxBusyCursor() { wxEndBusyCursor(); }
};
