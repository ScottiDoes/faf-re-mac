#pragma once

// wx stand-in: the application object, wxEntry and the key code mapping (see wx/defs.h).
//
// wxEntry builds the application through the IMPLEMENT_APP_NO_MAIN initializer, sets
// wxTheApp and runs OnInit, as the library does.
//
// With the window system up, the application pumps the Window area's message queue as wx
// 2.4.2 MSW does: Pending() peeks it, Dispatch()/DoMessage() take a message, give the window
// chain its ProcessMessage() turn (accelerators, dialog keys), then TranslateMessage and
// DispatchMessage; ProcessIdle() sends wxIdleEvents to the application and to every window
// (which is where wx generates wxEVT_LEAVE_WINDOW). Headless there is no queue: Pending() is
// false, and ProcessIdle() delivers pending events and deletes the windows queued for
// destruction (wxPendingDelete).

#include "wx/faf/toplevel.h"

class wxApp;
typedef wxApp* (*wxAppInitializerFunction)();

class wxBuildOptions
{
public:
  wxBuildOptions() = default;
};

class wxAppBase : public wxEvtHandler
{
public:
  wxAppBase() = default;
  ~wxAppBase() override = default;

  virtual bool OnInitGui() { return true; }
  virtual bool OnInit() { return true; }
  virtual int OnRun() { return MainLoop(); }
  virtual int OnExit() { return 0; }
  virtual void OnFatalException() {}

  virtual int MainLoop() = 0;
  virtual void ExitMainLoop() = 0;
  virtual bool Initialized() = 0;
  virtual bool Pending() = 0;
  virtual void Dispatch() = 0;
  virtual bool Yield(bool onlyIfNeeded = false) = 0;

  virtual void SetTopWindow(wxWindow* window) { m_topWindow = window; }
  virtual wxWindow* GetTopWindow() const
  {
    if (m_topWindow != nullptr) {
      return m_topWindow;
    }
    return wxTopLevelWindows.GetFirst();
  }
  void SetExitOnFrameDelete(bool flag) { m_exitOnFrameDelete = flag ? Yes : No; }
  bool GetExitOnFrameDelete() const { return m_exitOnFrameDelete == Yes; }
  wxString GetAppName() const { return m_appName.IsEmpty() ? m_className : m_appName; }
  void SetAppName(const wxString& name) { m_appName = name; }
  wxString GetClassName() const { return m_className; }
  void SetClassName(const wxString& name) { m_className = name; }
  const wxString& GetVendorName() const { return m_vendorName; }
  void SetVendorName(const wxString& name) { m_vendorName = name; }
  void SetUseBestVisual(bool flag) { m_useBestVisual = flag; }
  bool GetUseBestVisual() const { return m_useBestVisual; }
  virtual void SetActive(bool isActive, wxWindow* lastFocus)
  {
    (void)lastFocus;
    m_isActive = isActive;
  }
  bool IsActive() const { return m_isActive; }

  void ProcessPendingEvents() { wxEvtHandler::ProcessAllPendingEvents(); }

  static void SetInitializerFunction(wxAppInitializerFunction fn) { m_appInitFn = fn; }
  static wxAppInitializerFunction GetInitializerFunction() { return m_appInitFn; }
  static bool CheckBuildOptions(const wxBuildOptions& buildOptions)
  {
    (void)buildOptions;
    return true;
  }

  int argc = 0;
  wxChar** argv = nullptr;

protected:
  enum
  {
    Later = -1,
    No,
    Yes
  } m_exitOnFrameDelete = Later;

  wxString m_vendorName;
  wxString m_appName;
  wxString m_className;
  wxWindow* m_topWindow = nullptr;
  bool m_useBestVisual = false;
  bool m_isActive = true;

  inline static wxAppInitializerFunction m_appInitFn = nullptr;
};

// The application object (wxApp is a pointer in wx 2.4.2: WXDLLEXPORT_DATA(extern wxApp*)).
inline wxApp* wxTheApp = nullptr;

class wxApp : public wxAppBase
{
public:
  wxApp() = default;
  ~wxApp() override = default;

  bool Initialized() override { return m_initialized; }
  int MainLoop() override
  {
    m_keepGoing = TRUE;
    while (m_keepGoing) {
      while (!Pending() && ProcessIdle()) {
      }
      if (!m_keepGoing) {
        break;
      }
      if (Pending()) {
        Dispatch();
      } else {
        FAF_POSIX_STUB("wxApp::MainLoop: no native event source in the wx stand-in, leaving the loop");
        break;
      }
    }
    return 0;
  }
  void ExitMainLoop() override { m_keepGoing = FALSE; }
  bool Pending() override
  {
    if (!faf_wx_detail::NativeWindows()) {
      return false;
    }
    MSG message{};
    return ::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE) != FALSE;
  }
  void Dispatch() override { (void)DoMessage(); }
  bool Yield(bool onlyIfNeeded = false) override
  {
    (void)onlyIfNeeded;
    while (ProcessIdle()) {
    }
    return true;
  }

  // Headless: delivers pending events and deletes the top-level windows queued by Destroy(),
  // then reports that no more idle time is wanted. With the window system: wxApp::ProcessIdle
  // (a wxIdleEvent to the application, whose default handling is wxApp::OnIdle: pending
  // events, pending deletes, then idle events to every window).
  bool ProcessIdle()
  {
    if (!faf_wx_detail::NativeWindows()) {
      ProcessPendingEvents();
      DeletePendingObjects();
      return false;
    }
    wxIdleEvent event;
    event.SetEventObject(this);
    if (!ProcessEvent(event)) {
      FafDefaultOnIdle(event);
    }
    return event.MoreRequested();
  }
  void FafDefaultOnIdle(wxIdleEvent& event)
  {
    ProcessPendingEvents();
    DeletePendingObjects();
    if (SendIdleEvents()) {
      event.RequestMore(true);
    }
  }
  bool SendIdleEvents()
  {
    if (!faf_wx_detail::NativeWindows()) {
      return false;
    }
    bool needMore = false;
    std::vector<wxWindow*> windows(wxTopLevelWindows.begin(), wxTopLevelWindows.end());
    for (wxWindow* window : windows) {
      if (wxTopLevelWindows.Member(window) && SendIdleEvents(window)) {
        needMore = true;
      }
    }
    return needMore;
  }
  bool SendIdleEvents(wxWindow* window);
  void DeletePendingObjects()
  {
    while (!wxPendingDelete.IsEmpty()) {
      wxObject* const object = wxPendingDelete.GetFirst();
      wxPendingDelete.DeleteObject(object);
      delete object;
    }
  }
  // wxApp::ProcessMessage: accelerators (MSWTranslateMessage up to the top-level window),
  // then the parents' MSWProcessMessage (dialog navigation).
  virtual bool ProcessMessage(WXMSG* message);
  void DoMessage(WXMSG* message)
  {
    if (message == nullptr) {
      return;
    }
    if (!ProcessMessage(message)) {
      ::TranslateMessage(message);
      ::DispatchMessageW(message);
    }
  }
  // Takes one message (waiting for it) and processes it; false and the loop told to stop
  // on WM_QUIT, or headless where there is nothing to take.
  bool DoMessage()
  {
    if (!faf_wx_detail::NativeWindows()) {
      return false;
    }
    MSG message{};
    if (::GetMessageW(&message, nullptr, 0, 0) == FALSE) {
      m_keepGoing = FALSE;
      return false;
    }
    DoMessage(&message);
    return true;
  }
  void OnIdle(wxIdleEvent& event) { (void)event; }
  void OnEndSession(wxCloseEvent& event) { (void)event; }
  void OnQueryEndSession(wxCloseEvent& event) { (void)event; }
  int GetPrintMode() const { return 0; }
  void SetPrintMode(int mode) { (void)mode; }
  void SetAuto3D(bool flag) { (void)flag; }
  bool GetAuto3D() const { return false; }

  static bool Initialize() { return true; }
  static void CleanUp()
  {
    if (wxTheApp != nullptr) {
      wxTheApp->DeletePendingObjects();
    }
    wxImage::CleanUpHandlers();
    delete wxTheApp;
    wxTheApp = nullptr;
  }
  static bool RegisterWindowClasses() { return true; }
  static void UnregisterWindowClasses() {}

  // wxEntry marks the application initialised once it is built.
  void FafSetInitialized(bool initialized) { m_initialized = initialized; }

protected:
  int m_keepGoing = FALSE;
  bool m_initialized = false;
};

class wxAppInitializer
{
public:
  explicit wxAppInitializer(wxAppInitializerFunction fn) { wxApp::SetInitializerFunction(fn); }
};

// wx/app.h. IMPLEMENT_APP_NO_MAIN defines the factory and wxGetApp exactly as the library's
// macro does; DECLARE_APP declares the accessor.
#define DECLARE_APP(appname) extern appname& wxGetApp();
#define IMPLEMENT_APP_NO_MAIN(appname)                                                    \
  wxApp* wxCreateApp()                                                                    \
  {                                                                                       \
    wxApp::CheckBuildOptions(wxBuildOptions());                                           \
    return new appname;                                                                   \
  }                                                                                       \
  wxAppInitializer wxTheAppInitializer((wxAppInitializerFunction)wxCreateApp);            \
  appname& wxGetApp()                                                                     \
  {                                                                                       \
    return *static_cast<appname*>(wxTheApp);                                              \
  }
#define IMPLEMENT_WX_THEME_SUPPORT
#define IMPLEMENT_APP_NO_THEMES(appname) IMPLEMENT_APP_NO_MAIN(appname)

// wx 2.4.2 MSW wxEntry: initialise, build the app, OnInit; with enterLoop also run and clean
// up. Without it, returns 1 after a successful OnInit and leaves the app to the caller.
inline int wxEntry(WXHINSTANCE hInstance, WXHINSTANCE hPrevInstance, char* cmdLine, int cmdShow, bool enterLoop = true)
{
  (void)hInstance;
  (void)hPrevInstance;
  (void)cmdLine;
  (void)cmdShow;
  if (!wxApp::Initialize()) {
    return 0;
  }
  if (wxApp::GetInitializerFunction() == nullptr) {
    std::fprintf(stderr, "wxEntry: no application initializer (IMPLEMENT_APP_NO_MAIN missing)\n");
    return 0;
  }
  wxTheApp = (*wxApp::GetInitializerFunction())();
  if (wxTheApp == nullptr) {
    return 0;
  }
  wxTheApp->FafSetInitialized(true);
  int retValue = 0;
  if (wxTheApp->OnInitGui() && wxTheApp->OnInit()) {
    if (!enterLoop) {
      return 1;
    }
    retValue = wxTheApp->OnRun();
  }
  wxTheApp->OnExit();
  wxApp::CleanUp();
  return retValue;
}

inline bool wxYield()
{
  return wxTheApp != nullptr ? wxTheApp->Yield() : true;
}

inline bool wxSafeYield(wxWindow* window = nullptr, bool onlyIfNeeded = false)
{
  (void)window;
  (void)onlyIfNeeded;
  return wxYield();
}

inline void wxWakeUpIdle() {}

inline void wxExit()
{
  if (wxTheApp != nullptr) {
    wxTheApp->ExitMainLoop();
  }
}

inline WXHINSTANCE wxGetInstance()
{
  return 0;
}

inline void wxSetInstance(WXHINSTANCE instance)
{
  (void)instance;
}

inline long wxNewId()
{
  static long sLastId = 100;
  return ++sLastId;
}

inline void wxRegisterId(long id)
{
  (void)id;
}

// --- Key codes (wx 2.4.2 msw/window.cpp) --------------------------------------------------------
// The engine converts between Maui key codes (wxKeyCode values) and Windows virtual keys with
// these two; VK_* come from the Window area of the platform layer.
inline int wxCharCodeMSWToWX(int keySym)
{
  switch (keySym) {
    case VK_CANCEL:
      return WXK_CANCEL;
    case VK_BACK:
      return WXK_BACK;
    case VK_TAB:
      return WXK_TAB;
    case VK_CLEAR:
      return WXK_CLEAR;
    case VK_RETURN:
      return WXK_RETURN;
    case VK_SHIFT:
      return WXK_SHIFT;
    case VK_CONTROL:
      return WXK_CONTROL;
    case VK_MENU:
      return WXK_MENU;
    case VK_PAUSE:
      return WXK_PAUSE;
    case VK_CAPITAL:
      return WXK_CAPITAL;
    case VK_SPACE:
      return WXK_SPACE;
    case VK_ESCAPE:
      return WXK_ESCAPE;
    case VK_PRIOR:
      return WXK_PRIOR;
    case VK_NEXT:
      return WXK_NEXT;
    case VK_END:
      return WXK_END;
    case VK_HOME:
      return WXK_HOME;
    case VK_LEFT:
      return WXK_LEFT;
    case VK_UP:
      return WXK_UP;
    case VK_RIGHT:
      return WXK_RIGHT;
    case VK_DOWN:
      return WXK_DOWN;
    case VK_SELECT:
      return WXK_SELECT;
    case VK_PRINT:
      return WXK_PRINT;
    case VK_EXECUTE:
      return WXK_EXECUTE;
    case VK_INSERT:
      return WXK_INSERT;
    case VK_DELETE:
      return WXK_DELETE;
    case VK_HELP:
      return WXK_HELP;
    case VK_NUMPAD0:
      return WXK_NUMPAD0;
    case VK_NUMPAD1:
      return WXK_NUMPAD1;
    case VK_NUMPAD2:
      return WXK_NUMPAD2;
    case VK_NUMPAD3:
      return WXK_NUMPAD3;
    case VK_NUMPAD4:
      return WXK_NUMPAD4;
    case VK_NUMPAD5:
      return WXK_NUMPAD5;
    case VK_NUMPAD6:
      return WXK_NUMPAD6;
    case VK_NUMPAD7:
      return WXK_NUMPAD7;
    case VK_NUMPAD8:
      return WXK_NUMPAD8;
    case VK_NUMPAD9:
      return WXK_NUMPAD9;
    case VK_MULTIPLY:
      return WXK_MULTIPLY;
    case VK_ADD:
      return WXK_ADD;
    case VK_SUBTRACT:
      return WXK_SUBTRACT;
    case VK_DECIMAL:
      return WXK_DECIMAL;
    case VK_DIVIDE:
      return WXK_DIVIDE;
    case VK_F1:
      return WXK_F1;
    case VK_F2:
      return WXK_F2;
    case VK_F3:
      return WXK_F3;
    case VK_F4:
      return WXK_F4;
    case VK_F5:
      return WXK_F5;
    case VK_F6:
      return WXK_F6;
    case VK_F7:
      return WXK_F7;
    case VK_F8:
      return WXK_F8;
    case VK_F9:
      return WXK_F9;
    case VK_F10:
      return WXK_F10;
    case VK_F11:
      return WXK_F11;
    case VK_F12:
      return WXK_F12;
    case VK_F13:
      return WXK_F13;
    case VK_F14:
      return WXK_F14;
    case VK_F15:
      return WXK_F15;
    case VK_F16:
      return WXK_F16;
    case VK_F17:
      return WXK_F17;
    case VK_F18:
      return WXK_F18;
    case VK_F19:
      return WXK_F19;
    case VK_F20:
      return WXK_F20;
    case VK_F21:
      return WXK_F21;
    case VK_F22:
      return WXK_F22;
    case VK_F23:
      return WXK_F23;
    case VK_F24:
      return WXK_F24;
    case VK_NUMLOCK:
      return WXK_NUMLOCK;
    case VK_SCROLL:
      return WXK_SCROLL;
    case VK_OEM_1:
      return ';';
    case VK_OEM_PLUS:
      return '+';
    case VK_OEM_COMMA:
      return ',';
    case VK_OEM_MINUS:
      return '-';
    case VK_OEM_PERIOD:
      return '.';
    case VK_OEM_2:
      return '/';
    case VK_OEM_3:
      return '~';
    case VK_OEM_4:
      return '[';
    case VK_OEM_5:
      return '\\';
    case VK_OEM_6:
      return ']';
    case VK_OEM_7:
      return '\'';
    case VK_LWIN:
      return WXK_WINDOWS_LEFT;
    case VK_RWIN:
      return WXK_WINDOWS_RIGHT;
    case VK_APPS:
      return WXK_WINDOWS_MENU;
    default:
      return 0;
  }
}

inline int wxCharCodeWXToMSW(int id, bool* isVirtual)
{
  *isVirtual = true;
  switch (id) {
    case WXK_CANCEL:
      return VK_CANCEL;
    case WXK_CLEAR:
      return VK_CLEAR;
    case WXK_SHIFT:
      return VK_SHIFT;
    case WXK_CONTROL:
      return VK_CONTROL;
    case WXK_MENU:
      return VK_MENU;
    case WXK_PAUSE:
      return VK_PAUSE;
    case WXK_PRIOR:
      return VK_PRIOR;
    case WXK_NEXT:
      return VK_NEXT;
    case WXK_END:
      return VK_END;
    case WXK_HOME:
      return VK_HOME;
    case WXK_LEFT:
      return VK_LEFT;
    case WXK_UP:
      return VK_UP;
    case WXK_RIGHT:
      return VK_RIGHT;
    case WXK_DOWN:
      return VK_DOWN;
    case WXK_SELECT:
      return VK_SELECT;
    case WXK_PRINT:
      return VK_PRINT;
    case WXK_EXECUTE:
      return VK_EXECUTE;
    case WXK_INSERT:
      return VK_INSERT;
    case WXK_DELETE:
      return VK_DELETE;
    case WXK_HELP:
      return VK_HELP;
    case WXK_NUMPAD0:
      return VK_NUMPAD0;
    case WXK_NUMPAD1:
      return VK_NUMPAD1;
    case WXK_NUMPAD2:
      return VK_NUMPAD2;
    case WXK_NUMPAD3:
      return VK_NUMPAD3;
    case WXK_NUMPAD4:
      return VK_NUMPAD4;
    case WXK_NUMPAD5:
      return VK_NUMPAD5;
    case WXK_NUMPAD6:
      return VK_NUMPAD6;
    case WXK_NUMPAD7:
      return VK_NUMPAD7;
    case WXK_NUMPAD8:
      return VK_NUMPAD8;
    case WXK_NUMPAD9:
      return VK_NUMPAD9;
    case WXK_MULTIPLY:
      return VK_MULTIPLY;
    case WXK_ADD:
      return VK_ADD;
    case WXK_SUBTRACT:
      return VK_SUBTRACT;
    case WXK_DECIMAL:
      return VK_DECIMAL;
    case WXK_DIVIDE:
      return VK_DIVIDE;
    case WXK_F1:
      return VK_F1;
    case WXK_F2:
      return VK_F2;
    case WXK_F3:
      return VK_F3;
    case WXK_F4:
      return VK_F4;
    case WXK_F5:
      return VK_F5;
    case WXK_F6:
      return VK_F6;
    case WXK_F7:
      return VK_F7;
    case WXK_F8:
      return VK_F8;
    case WXK_F9:
      return VK_F9;
    case WXK_F10:
      return VK_F10;
    case WXK_F11:
      return VK_F11;
    case WXK_F12:
      return VK_F12;
    case WXK_F13:
      return VK_F13;
    case WXK_F14:
      return VK_F14;
    case WXK_F15:
      return VK_F15;
    case WXK_F16:
      return VK_F16;
    case WXK_F17:
      return VK_F17;
    case WXK_F18:
      return VK_F18;
    case WXK_F19:
      return VK_F19;
    case WXK_F20:
      return VK_F20;
    case WXK_F21:
      return VK_F21;
    case WXK_F22:
      return VK_F22;
    case WXK_F23:
      return VK_F23;
    case WXK_F24:
      return VK_F24;
    case WXK_NUMLOCK:
      return VK_NUMLOCK;
    case WXK_SCROLL:
      return VK_SCROLL;
    default:
      *isVirtual = false;
      return id;
  }
}
