// Test of the shell package: the SDL3 window system behind the Window area's HWNDs and the
// wx stand-in. Linked with the whole engine (macos/tests/shell/build_test.sh), so it exercises
// the real Window area, the wx stand-in and the shell together.
//
// It creates a frame + child viewport the way CScApp does (WSupComFrame / WD3DViewport),
// checks the HWNDs, rectangles in drawable pixels, the CAMetalLayer of the viewport (and of
// a child that does not fill the frame), then injects SDL events (SDL_PushEvent) and checks
// the Win32 messages each window procedure receives and the wx events the stand-in makes of
// them: mouse, double click, wheel, keys, characters (UTF-16), modifiers, focus, activation,
// resize (the sizing sequence), move, close, quit, paint; plus the cursor, monitors, key
// state, the message queue, MsgWaitForMultipleObjectsEx and teardown.
//
// GUI etiquette: windows are small, shown for a moment without taking the focus (no
// activation), and destroyed; SHELL_TEST_SHOW=0 keeps them hidden. A watchdog ends the
// process after 60 s. The clipboard (SHELL_TEST_CLIPBOARD=1) and pointer warp
// (SHELL_TEST_WARP=1) checks touch the user's session and are off by default.

#include <wx/wx.h>

#include "platform/posix/shell/ShellInternal.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
void* sel_registerName(const char* name);
void objc_msgSend(void);
}

namespace
{
  int gPasses = 0;
  int gFailures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++gPasses;                                                             \
    } else {                                                                 \
      ++gFailures;                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                                                 \
  do {                                                                                                 \
    const long long va_ = static_cast<long long>(a);                                                   \
    const long long vb_ = static_cast<long long>(b);                                                   \
    if (va_ == vb_) {                                                                                  \
      ++gPasses;                                                                                       \
    } else {                                                                                           \
      ++gFailures;                                                                                     \
      std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_);     \
    }                                                                                                  \
  } while (0)

  template <typename Result, typename... Args>
  Result Objc(void* receiver, const char* selector, Args... args)
  {
    using Function = Result (*)(void*, void*, Args...);
    return reinterpret_cast<Function>(&objc_msgSend)(receiver, sel_registerName(selector), args...);
  }
  struct CGSizeLike
  {
    double width;
    double height;
  };
  struct CGRectLike
  {
    double x;
    double y;
    double width;
    double height;
  };

  struct Message
  {
    HWND hwnd;
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
  };
  std::vector<Message> gMessages;

  struct WxRecord
  {
    wxEventType type;
    std::string who;
    int x = 0;
    int y = 0;
    long keyCode = 0;
    wxUint32 rawCode = 0;
    wxUint32 rawFlags = 0;
    int wheel = 0;
    bool left = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool active = false;
    int width = 0;
    int height = 0;
  };
  std::vector<WxRecord> gEvents;

  void Clear()
  {
    gMessages.clear();
    gEvents.clear();
  }

  const Message* FindMessage(HWND hwnd, UINT msg, std::size_t nth = 0)
  {
    for (const Message& m : gMessages) {
      if (m.hwnd == hwnd && m.msg == msg) {
        if (nth == 0) {
          return &m;
        }
        --nth;
      }
    }
    return nullptr;
  }

  int CountMessages(HWND hwnd, UINT msg)
  {
    int count = 0;
    for (const Message& m : gMessages) {
      count += m.hwnd == hwnd && m.msg == msg ? 1 : 0;
    }
    return count;
  }

  // Index of the first message `msg` to `hwnd` at or after `from`; -1 if none.
  int IndexOf(HWND hwnd, UINT msg, int from = 0)
  {
    for (int i = from; i < static_cast<int>(gMessages.size()); ++i) {
      if (gMessages[static_cast<std::size_t>(i)].hwnd == hwnd && gMessages[static_cast<std::size_t>(i)].msg == msg) {
        return i;
      }
    }
    return -1;
  }

  const WxRecord* FindEvent(wxEventType type, const char* who, std::size_t nth = 0)
  {
    for (const WxRecord& e : gEvents) {
      if (e.type == type && e.who == who) {
        if (nth == 0) {
          return &e;
        }
        --nth;
      }
    }
    return nullptr;
  }

  void PushAndPump(SDL_Event event)
  {
    SDL_PushEvent(&event);
    faf_posix::shell::PumpEvents();
  }

  void PumpFor(int milliseconds)
  {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
      faf_posix::shell::PumpEvents();
      SDL_Delay(5);
    }
  }

  void RecordMouse(const char* who, wxMouseEvent& event)
  {
    WxRecord r{};
    r.type = event.GetEventType();
    r.who = who;
    r.x = event.m_x;
    r.y = event.m_y;
    r.wheel = event.m_wheelRotation;
    r.left = event.m_leftDown;
    r.shift = event.m_shiftDown;
    r.ctrl = event.m_controlDown;
    r.alt = event.m_altDown;
    gEvents.push_back(r);
    event.Skip();
  }

  void RecordKey(const char* who, wxKeyEvent& event)
  {
    WxRecord r{};
    r.type = event.GetEventType();
    r.who = who;
    r.keyCode = event.m_keyCode;
    r.rawCode = event.m_rawCode;
    r.rawFlags = event.m_rawFlags;
    r.shift = event.m_shiftDown;
    r.ctrl = event.m_controlDown;
    r.alt = event.m_altDown;
    gEvents.push_back(r);
  }

  // The game viewport's stand-in: a plain child wxWindow (WD3DViewport is one).
  class TestViewport : public wxWindow
  {
  public:
    TestViewport(wxWindow* parent, const wxSize& size) : wxWindow(parent, -1, wxDefaultPosition, size, 0, L"viewport") {}

    long MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override
    {
      gMessages.push_back({reinterpret_cast<HWND>(GetHandle()), msg, wParam, lParam});
      return wxWindow::MSWWindowProc(msg, wParam, lParam);
    }

    void OnMouse(wxMouseEvent& event) { RecordMouse("viewport", event); }
    void OnKeyDown(wxKeyEvent& event)
    {
      RecordKey("viewport", event);
      // A handled key-down (no Skip) for 'B' must swallow its WM_CHAR.
      if (event.m_rawCode != 'B') {
        event.Skip();
      }
    }
    void OnKeyUp(wxKeyEvent& event)
    {
      RecordKey("viewport", event);
      event.Skip();
    }
    void OnChar(wxKeyEvent& event)
    {
      RecordKey("viewport", event);
      event.Skip();
    }
    void OnSize(wxSizeEvent& event)
    {
      WxRecord r{};
      r.type = wxEVT_SIZE;
      r.who = "viewport";
      r.width = event.GetSize().x;
      r.height = event.GetSize().y;
      gEvents.push_back(r);
      event.Skip();
    }
    void OnFocus(wxFocusEvent& event)
    {
      WxRecord r{};
      r.type = event.GetEventType();
      r.who = "viewport";
      gEvents.push_back(r);
      event.Skip();
    }
    void OnPaint(wxPaintEvent& event)
    {
      (void)event;
      WxRecord r{};
      r.type = wxEVT_PAINT;
      r.who = "viewport";
      gEvents.push_back(r);
    }

    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(TestViewport, wxWindow)
    EVT_MOUSE_EVENTS(TestViewport::OnMouse)
    EVT_KEY_DOWN(TestViewport::OnKeyDown)
    EVT_KEY_UP(TestViewport::OnKeyUp)
    EVT_CHAR(TestViewport::OnChar)
    EVT_SIZE(TestViewport::OnSize)
    EVT_SET_FOCUS(TestViewport::OnFocus)
    EVT_KILL_FOCUS(TestViewport::OnFocus)
    EVT_PAINT(TestViewport::OnPaint)
  END_EVENT_TABLE()

  // WSupComFrame's stand-in: a captioned, resizable frame.
  class TestFrame : public wxFrame
  {
  public:
    TestFrame(const wxSize& size, long style) : wxFrame(nullptr, -1, L"faf shell test", wxDefaultPosition, size, style) {}

    long MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override
    {
      gMessages.push_back({reinterpret_cast<HWND>(GetHandle()), msg, wParam, lParam});
      return wxFrame::MSWWindowProc(msg, wParam, lParam);
    }

    void OnClose(wxCloseEvent& event)
    {
      WxRecord r{};
      r.type = wxEVT_CLOSE_WINDOW;
      r.who = "frame";
      gEvents.push_back(r);
      event.Veto(); // the game's frame asks first (escape dialog); it stays
    }
    void OnActivate(wxActivateEvent& event)
    {
      WxRecord r{};
      r.type = wxEVT_ACTIVATE;
      r.who = "frame";
      r.active = event.GetActive();
      gEvents.push_back(r);
      event.Skip();
    }
    void OnSize(wxSizeEvent& event)
    {
      WxRecord r{};
      r.type = wxEVT_SIZE;
      r.who = "frame";
      r.width = event.GetSize().x;
      r.height = event.GetSize().y;
      gEvents.push_back(r);
      event.Skip(); // the frame's default layout gives the viewport the client area
    }
    void OnMove(wxMoveEvent& event)
    {
      WxRecord r{};
      r.type = wxEVT_MOVE;
      r.who = "frame";
      r.x = event.GetPosition().x;
      r.y = event.GetPosition().y;
      gEvents.push_back(r);
    }

    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(TestFrame, wxFrame)
    EVT_CLOSE(TestFrame::OnClose)
    EVT_ACTIVATE(TestFrame::OnActivate)
    EVT_SIZE(TestFrame::OnSize)
    EVT_MOVE(TestFrame::OnMove)
  END_EVENT_TABLE()

  // A handler pushed on the viewport, as the UI pushes CUIKeyHandler and the Maui event
  // mapper: it sees every event before the window's own table.
  class PushedHandler : public wxEvtHandler
  {
  public:
    void OnMouse(wxMouseEvent& event) { RecordMouse("pushed", event); }
    void OnKeyDown(wxKeyEvent& event)
    {
      RecordKey("pushed", event);
      event.Skip();
    }
    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(PushedHandler, wxEvtHandler)
    EVT_MOUSE_EVENTS(PushedHandler::OnMouse)
    EVT_KEY_DOWN(PushedHandler::OnKeyDown)
  END_EVENT_TABLE()

  class TestApp : public wxApp
  {};

  SDL_Event KeyEvent(SDL_WindowID window, bool down, SDL_Scancode scancode, SDL_Keycode key, SDL_Keymod mod, bool repeat = false)
  {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.windowID = window;
    e.key.scancode = scancode;
    e.key.key = key;
    e.key.mod = mod;
    e.key.down = down;
    e.key.repeat = repeat;
    return e;
  }

  SDL_Event TextEvent(SDL_WindowID window, const char* text)
  {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.windowID = window;
    e.text.text = text;
    return e;
  }

  SDL_Event ButtonEvent(SDL_WindowID window, bool down, Uint8 button, Uint8 clicks, float x, float y)
  {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = window;
    e.button.button = button;
    e.button.down = down;
    e.button.clicks = clicks;
    e.button.x = x;
    e.button.y = y;
    return e;
  }

  SDL_Event WindowEvent(SDL_WindowID window, Uint32 type)
  {
    SDL_Event e{};
    e.type = type;
    e.window.windowID = window;
    return e;
  }

  bool EnvOn(const char* name, bool fallback)
  {
    const char* const value = std::getenv(name);
    return value == nullptr ? fallback : std::strcmp(value, "0") != 0;
  }
} // namespace

int main()
{
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(60));
    std::fprintf(stderr, "shell_test: watchdog: 60 s, exiting\n");
    std::_Exit(3);
  }).detach();
  setvbuf(stdout, nullptr, _IONBF, 0);
  const bool show = EnvOn("SHELL_TEST_SHOW", true);

  // --- 0. Headless: no window system, the old answers -------------------------------------
  std::printf("== headless\n");
  CHECK(!faf_posix::shell::IsActive());
  {
    auto* const frame = new wxFrame(nullptr, -1, L"headless", wxDefaultPosition, wxSize(300, 200));
    CHECK(frame->GetHandle() == 0);
    CHECK(frame->GetSize() == wxSize(300, 200));
    auto* const child = new wxWindow(frame, -1, wxPoint(5, 6), wxSize(30, 40));
    CHECK(child->GetHandle() == 0);
    CHECK(child->GetPosition() == wxPoint(5, 6));
    delete frame;
  }
  CHECK_EQ(::GetSystemMetrics(SM_CXSCREEN), 1920);
  CHECK(faf_posix::shell::MetalLayer(nullptr) == nullptr);
  CHECK(!faf_posix::shell::PumpEvents());
  MSG msg{};
  CHECK(::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) == FALSE);
  CHECK(::CreateWindowExW(0, L"x", L"x", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr) == nullptr);
  CHECK(::GetKeyState(VK_SHIFT) == 0);

  // --- 1. Init -------------------------------------------------------------------------------
  std::printf("== init\n");
  // Test windows never take the focus: a background application, windows shown without
  // activation (focus changes are injected instead).
  SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
  SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
  CHECK(faf_posix::shell::Init());
  CHECK(faf_posix::shell::IsActive());
  TestApp app;
  wxTheApp = &app;
  const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
  const int screenH = ::GetSystemMetrics(SM_CYSCREEN);
  std::printf("   screen %dx%d px, %d monitor(s)\n", screenW, screenH, ::GetSystemMetrics(SM_CMONITORS));
  CHECK(screenW > 0 && screenH > 0);

  // --- 2. Frame + viewport (CScApp::CreateAppFrame's shape) -----------------------------------
  std::printf("== frame and viewport\n");
  constexpr long kWindowedFrameStyle = 0x20400E40; // CScApp: caption, resize, min/max, system menu
  auto* const frame = new TestFrame(wxSize(400, 300), kWindowedFrameStyle);
  const HWND frameHwnd = reinterpret_cast<HWND>(frame->GetHandle());
  CHECK(frameHwnd != nullptr);
  CHECK(::IsWindow(frameHwnd) != FALSE);
  CHECK(::IsWindowVisible(frameHwnd) == FALSE); // top-level windows start hidden
  frame->SetSizeHints(320, 240);
  frame->SetClientSize(400, 300);
  int clientW = 0;
  int clientH = 0;
  frame->GetClientSize(&clientW, &clientH);
  CHECK_EQ(clientW, 400);
  CHECK_EQ(clientH, 300);
  const float density = faf_posix::shell::PixelDensity(frameHwnd);
  std::printf("   pixel density %.2f\n", density);
  CHECK(density >= 1.0f);
  RECT client{};
  RECT window{};
  CHECK(::GetClientRect(frameHwnd, &client) != FALSE);
  CHECK(::GetWindowRect(frameHwnd, &window) != FALSE);
  CHECK_EQ(client.right, 400);
  CHECK_EQ(client.bottom, 300);
  std::printf("   frame window rect %ld,%ld-%ld,%ld (title bar %ld px)\n", static_cast<long>(window.left),
              static_cast<long>(window.top), static_cast<long>(window.right), static_cast<long>(window.bottom),
              static_cast<long>(window.bottom - window.top - client.bottom));
  CHECK_EQ(window.right - window.left, 400);
  CHECK(window.bottom - window.top >= 300);

  auto* const viewport = new TestViewport(frame, wxSize(clientW, clientH));
  const HWND viewportHwnd = reinterpret_cast<HWND>(viewport->GetHandle());
  CHECK(viewportHwnd != nullptr);
  CHECK(viewportHwnd != frameHwnd);
  CHECK(::GetParent(viewportHwnd) == frameHwnd);
  CHECK(::GetAncestor(viewportHwnd, GA_ROOT) == frameHwnd);
  CHECK(::IsChild(frameHwnd, viewportHwnd) != FALSE);
  CHECK((::GetWindowLongPtrW(viewportHwnd, GWL_STYLE) & WS_CHILD) != 0);
  CHECK(::GetClientRect(viewportHwnd, &client) != FALSE);
  CHECK_EQ(client.right, 400);
  CHECK_EQ(client.bottom, 300);
  CHECK(viewport->GetPosition() == wxPoint(0, 0));
  CHECK(wxFindWinFromHandle(reinterpret_cast<WXHWND>(viewportHwnd)) == viewport);

  // Coordinates: the viewport's client origin is the frame's.
  POINT a{0, 0};
  POINT b{0, 0};
  ::ClientToScreen(frameHwnd, &a);
  ::ClientToScreen(viewportHwnd, &b);
  CHECK_EQ(a.x, b.x);
  CHECK_EQ(a.y, b.y);
  POINT round{37, 21};
  ::ClientToScreen(viewportHwnd, &round);
  ::ScreenToClient(viewportHwnd, &round);
  CHECK_EQ(round.x, 37);
  CHECK_EQ(round.y, 21);

  // The Metal layer presents exactly the viewport's client area.
  void* const layer = faf_posix::shell::MetalLayer(viewportHwnd);
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    const CGSizeLike drawable = Objc<CGSizeLike>(layer, "drawableSize");
    const double scale = Objc<double>(layer, "contentsScale");
    std::printf("   viewport layer drawable %.0fx%.0f, contentsScale %.2f\n", drawable.width, drawable.height, scale);
    CHECK_EQ(std::lround(drawable.width), 400);
    CHECK_EQ(std::lround(drawable.height), 300);
    CHECK(std::fabs(scale - density) < 0.01);
    CHECK(faf_posix::shell::MetalLayer(viewportHwnd) == layer); // one layer per window
  }

  // A child that does not fill the frame: its own rectangle.
  {
    auto* const inset = new wxWindow(frame, -1, wxPoint(10, 20), wxSize(100, 60));
    const HWND insetHwnd = reinterpret_cast<HWND>(inset->GetHandle());
    void* const insetLayer = faf_posix::shell::MetalLayer(insetHwnd);
    CHECK(insetLayer != nullptr);
    if (insetLayer != nullptr) {
      const CGSizeLike drawable = Objc<CGSizeLike>(insetLayer, "drawableSize");
      CHECK_EQ(std::lround(drawable.width), 100);
      CHECK_EQ(std::lround(drawable.height), 60);
      void* const view = Objc<void*>(insetLayer, "delegate");
      if (view != nullptr) {
        const CGRectLike frameRect = Objc<CGRectLike>(view, "frame");
        void* const superview = Objc<void*>(view, "superview");
        const CGRectLike bounds = Objc<CGRectLike>(superview, "bounds");
        const bool flipped = Objc<bool>(superview, "isFlipped");
        const double top = flipped ? frameRect.y : bounds.height - frameRect.y - frameRect.height;
        std::printf("   inset view frame %.1f,%.1f %.1fx%.1f (points)\n", frameRect.x, top, frameRect.width, frameRect.height);
        CHECK(std::fabs(frameRect.x - 10.0 / density) < 0.01);
        CHECK(std::fabs(top - 20.0 / density) < 0.01);
        CHECK(std::fabs(frameRect.width - 100.0 / density) < 0.01);
      }
    }
    inset->SetSize(30, 40, 50, 50);
    if (insetLayer != nullptr) {
      const CGSizeLike drawable = Objc<CGSizeLike>(insetLayer, "drawableSize");
      CHECK_EQ(std::lround(drawable.width), 50);
    }
    delete inset;
    CHECK(::IsWindow(insetHwnd) == FALSE);
  }

  // --- 3. Show (without activation), paint ----------------------------------------------------
  if (show) {
    std::printf("== show\n");
    Clear();
    frame->Show(true);
    PumpFor(150);
    CHECK(::IsWindowVisible(frameHwnd) != FALSE);
    CHECK(::IsWindowVisible(viewportHwnd) != FALSE);
    CHECK(FindEvent(wxEVT_PAINT, "viewport") != nullptr);
    Clear();
    viewport->Refresh();
    faf_posix::shell::PumpEvents();
    CHECK_EQ(CountMessages(viewportHwnd, WM_PAINT), 1);
    CHECK(FindEvent(wxEVT_PAINT, "viewport") != nullptr);
    faf_posix::shell::PumpEvents();
    CHECK_EQ(CountMessages(viewportHwnd, WM_PAINT), 1); // validated as it was taken
  }

  SDL_Window* const sdlFrame = SDL_GetWindows(nullptr)[0];
  const SDL_WindowID frameId = SDL_GetWindowID(sdlFrame);

  // --- 4. Focus and activation ----------------------------------------------------------------
  std::printf("== focus and activation\n");
  Clear();
  viewport->SetFocus();
  CHECK(::GetFocus() == viewportHwnd);
  CHECK(wxWindow::FindFocus() == viewport);
  CHECK(FindMessage(viewportHwnd, WM_SETFOCUS) != nullptr);
  CHECK(FindEvent(wxEVT_SET_FOCUS, "viewport") != nullptr);
  // From inactive (whatever the real focus did meanwhile) to active.
  PushAndPump(WindowEvent(frameId, SDL_EVENT_WINDOW_FOCUS_LOST));
  std::printf("   real keyboard focus: %s\n", SDL_GetKeyboardFocus() == sdlFrame ? "the frame" : "elsewhere");
  Clear();
  PushAndPump(WindowEvent(frameId, SDL_EVENT_WINDOW_FOCUS_GAINED));
  CHECK(::GetForegroundWindow() == frameHwnd);
  CHECK(::GetActiveWindow() == frameHwnd);
  const int activateApp = IndexOf(frameHwnd, WM_ACTIVATEAPP);
  const int activate = IndexOf(frameHwnd, WM_ACTIVATE);
  CHECK(activateApp >= 0 && activate > activateApp);
  if (activate >= 0) {
    CHECK_EQ(LOWORD(gMessages[static_cast<std::size_t>(activate)].wParam), WA_ACTIVE);
  }
  if (const WxRecord* e = FindEvent(wxEVT_ACTIVATE, "frame"); e != nullptr) {
    CHECK(e->active);
  } else {
    CHECK(!"wxEVT_ACTIVATE");
  }
  CHECK(::GetFocus() == viewportHwnd); // focus stays on (or returns to) the viewport

  // --- 5. Mouse -------------------------------------------------------------------------------
  std::printf("== mouse\n");
  auto* const pushed = new PushedHandler();
  viewport->PushEventHandler(pushed);
  const auto px = [density](float points) { return static_cast<int>(std::floor(points * density + 1e-3f)); };
  Clear();
  {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = frameId;
    e.motion.x = 50.5f;
    e.motion.y = 40.0f;
    PushAndPump(e);
  }
  const Message* move = FindMessage(viewportHwnd, WM_MOUSEMOVE);
  CHECK(move != nullptr);
  if (move != nullptr) {
    CHECK_EQ(GET_X_LPARAM(move->lParam), px(50.5f));
    CHECK_EQ(GET_Y_LPARAM(move->lParam), px(40.0f));
  }
  CHECK(IndexOf(viewportHwnd, WM_SETCURSOR) >= 0 && IndexOf(viewportHwnd, WM_SETCURSOR) < IndexOf(viewportHwnd, WM_MOUSEMOVE));
  CHECK(FindEvent(wxEVT_ENTER_WINDOW, "viewport") != nullptr);
  CHECK(FindEvent(wxEVT_MOTION, "pushed") != nullptr);
  // The pushed handler first, then the window.
  {
    int pushedAt = -1;
    int windowAt = -1;
    for (int i = 0; i < static_cast<int>(gEvents.size()); ++i) {
      if (gEvents[static_cast<std::size_t>(i)].type == wxEVT_MOTION) {
        (gEvents[static_cast<std::size_t>(i)].who == "pushed" ? pushedAt : windowAt) = i;
      }
    }
    CHECK(pushedAt >= 0 && windowAt > pushedAt);
  }
  // WM_SETCURSOR went unhandled by the window: the class cursor (the arrow) is on.
  CHECK(::GetCursor() == ::LoadCursorW(nullptr, IDC_ARROW));
  if (const WxRecord* e = FindEvent(wxEVT_MOTION, "viewport")) {
    CHECK_EQ(e->x, px(50.5f));
    CHECK_EQ(e->y, px(40.0f));
  } else {
    CHECK(!"wxEVT_MOTION");
  }

  Clear();
  PushAndPump(ButtonEvent(frameId, true, SDL_BUTTON_LEFT, 1, 60.0f, 45.0f));
  PushAndPump(ButtonEvent(frameId, false, SDL_BUTTON_LEFT, 1, 60.0f, 45.0f));
  PushAndPump(ButtonEvent(frameId, true, SDL_BUTTON_LEFT, 2, 60.0f, 45.0f));
  PushAndPump(ButtonEvent(frameId, false, SDL_BUTTON_LEFT, 2, 60.0f, 45.0f));
  if (const Message* down = FindMessage(viewportHwnd, WM_LBUTTONDOWN)) {
    CHECK((down->wParam & MK_LBUTTON) != 0);
    CHECK_EQ(GET_X_LPARAM(down->lParam), px(60.0f));
  } else {
    CHECK(!"WM_LBUTTONDOWN");
  }
  if (const Message* up = FindMessage(viewportHwnd, WM_LBUTTONUP)) {
    CHECK((up->wParam & MK_LBUTTON) == 0);
  } else {
    CHECK(!"WM_LBUTTONUP");
  }
  CHECK(FindMessage(viewportHwnd, WM_LBUTTONDBLCLK) != nullptr);
  CHECK(FindEvent(wxEVT_LEFT_DOWN, "viewport") != nullptr);
  CHECK(FindEvent(wxEVT_LEFT_UP, "viewport") != nullptr);
  CHECK(FindEvent(wxEVT_LEFT_DCLICK, "viewport") != nullptr);
  if (const WxRecord* e = FindEvent(wxEVT_LEFT_DOWN, "viewport")) {
    CHECK(e->left);
  }

  Clear();
  PushAndPump(ButtonEvent(frameId, true, SDL_BUTTON_RIGHT, 1, 10.0f, 10.0f));
  PushAndPump(ButtonEvent(frameId, false, SDL_BUTTON_RIGHT, 1, 10.0f, 10.0f));
  PushAndPump(ButtonEvent(frameId, true, SDL_BUTTON_MIDDLE, 1, 10.0f, 10.0f));
  CHECK(::GetKeyState(VK_MBUTTON) < 0);
  PushAndPump(ButtonEvent(frameId, false, SDL_BUTTON_MIDDLE, 1, 10.0f, 10.0f));
  CHECK(::GetKeyState(VK_MBUTTON) >= 0);
  CHECK(FindEvent(wxEVT_RIGHT_DOWN, "viewport") != nullptr);
  CHECK(FindEvent(wxEVT_RIGHT_UP, "viewport") != nullptr);
  // Side buttons: WM_XBUTTON* with the button in HIWORD(wParam) (WD3DViewport turns them
  // into key presses).
  PushAndPump(ButtonEvent(frameId, true, SDL_BUTTON_X2, 1, 10.0f, 10.0f));
  PushAndPump(ButtonEvent(frameId, false, SDL_BUTTON_X2, 1, 10.0f, 10.0f));
  if (const Message* x = FindMessage(viewportHwnd, WM_XBUTTONDOWN)) {
    CHECK_EQ(HIWORD(x->wParam), XBUTTON2);
    CHECK((LOWORD(x->wParam) & MK_XBUTTON2) != 0);
  } else {
    CHECK(!"WM_XBUTTONDOWN");
  }
  CHECK(FindEvent(wxEVT_MIDDLE_DOWN, "viewport") != nullptr);
  CHECK(FindEvent(wxEVT_MIDDLE_UP, "viewport") != nullptr);
  // Outside the client area without a capture: nobody gets it.
  Clear();
  PushAndPump(ButtonEvent(frameId, true, SDL_BUTTON_LEFT, 1, 5000.0f, 10.0f));
  PushAndPump(ButtonEvent(frameId, false, SDL_BUTTON_LEFT, 1, 5000.0f, 10.0f));
  CHECK_EQ(CountMessages(viewportHwnd, WM_LBUTTONDOWN), 0);
  // With the capture: the captured window gets it, in its own client coordinates.
  viewport->CaptureMouse();
  CHECK(::GetCapture() == viewportHwnd);
  CHECK(viewport->HasCapture());
  PushAndPump(ButtonEvent(frameId, true, SDL_BUTTON_LEFT, 1, 1000.0f, 10.0f));
  if (const Message* down = FindMessage(viewportHwnd, WM_LBUTTONDOWN)) {
    CHECK_EQ(GET_X_LPARAM(down->lParam), px(1000.0f));
  } else {
    CHECK(!"captured WM_LBUTTONDOWN");
  }
  PushAndPump(ButtonEvent(frameId, false, SDL_BUTTON_LEFT, 1, 1000.0f, 10.0f));
  viewport->ReleaseMouse();
  CHECK(::GetCapture() == nullptr);
  CHECK(FindMessage(viewportHwnd, WM_CAPTURECHANGED) != nullptr);

  // Wheel: WHEEL_DELTA units in the physical direction, screen coordinates, to the focus.
  Clear();
  {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.windowID = frameId;
    e.wheel.y = 1.0f;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    e.wheel.mouse_x = 20.0f;
    e.wheel.mouse_y = 30.0f;
    PushAndPump(e);
    e.wheel.y = 1.0f;
    e.wheel.direction = SDL_MOUSEWHEEL_FLIPPED; // natural scrolling: the physical motion was down
    PushAndPump(e);
    e.wheel.y = 0.05f; // a trackpad's small steps add up: 6 + 6
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    PushAndPump(e);
    PushAndPump(e);
  }
  const Message* wheel = FindMessage(viewportHwnd, WM_MOUSEWHEEL);
  CHECK(wheel != nullptr);
  if (wheel != nullptr) {
    CHECK_EQ(GET_WHEEL_DELTA_WPARAM(wheel->wParam), WHEEL_DELTA);
    POINT expect{px(20.0f), px(30.0f)};
    ::ClientToScreen(frameHwnd, &expect);
    CHECK_EQ(GET_X_LPARAM(wheel->lParam), expect.x);
    CHECK_EQ(GET_Y_LPARAM(wheel->lParam), expect.y);
  }
  if (const Message* flipped = FindMessage(viewportHwnd, WM_MOUSEWHEEL, 1)) {
    CHECK_EQ(GET_WHEEL_DELTA_WPARAM(flipped->wParam), -WHEEL_DELTA);
  } else {
    CHECK(!"flipped wheel");
  }
  CHECK_EQ(CountMessages(viewportHwnd, WM_MOUSEWHEEL), 4);
  if (const WxRecord* e = FindEvent(wxEVT_MOUSEWHEEL, "viewport")) {
    CHECK_EQ(e->wheel, WHEEL_DELTA);
    int x = e->x;
    int y = e->y;
    viewport->ScreenToClient(&x, &y);
    CHECK_EQ(x, px(20.0f));
    CHECK_EQ(y, px(30.0f));
  } else {
    CHECK(!"wxEVT_MOUSEWHEEL");
  }

  // --- 6. Keyboard ----------------------------------------------------------------------------
  std::printf("== keyboard\n");
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE));
  CHECK(::GetKeyState('A') < 0);
  CHECK(::GetAsyncKeyState('A') < 0);
  PushAndPump(TextEvent(frameId, "a"));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE));
  CHECK(::GetKeyState('A') >= 0);
  if (const Message* down = FindMessage(viewportHwnd, WM_KEYDOWN)) {
    CHECK_EQ(down->wParam, 'A');
    CHECK_EQ((down->lParam >> 16) & 0xFF, 0x1E); // scan code
    CHECK_EQ(down->lParam & 0xFFFF, 1);          // repeat count
    CHECK((down->lParam & (1 << 30)) == 0);
  } else {
    CHECK(!"WM_KEYDOWN A");
  }
  if (const Message* up = FindMessage(viewportHwnd, WM_KEYUP)) {
    CHECK((up->lParam & (1u << 31)) != 0);
    CHECK((up->lParam & (1 << 30)) != 0);
  } else {
    CHECK(!"WM_KEYUP A");
  }
  if (const Message* ch = FindMessage(viewportHwnd, WM_CHAR)) {
    CHECK_EQ(ch->wParam, 'a');
  } else {
    CHECK(!"WM_CHAR a");
  }
  CHECK(IndexOf(viewportHwnd, WM_KEYDOWN) < IndexOf(viewportHwnd, WM_CHAR));
  if (const WxRecord* e = FindEvent(wxEVT_KEY_DOWN, "viewport")) {
    CHECK_EQ(e->keyCode, 'A');
    CHECK_EQ(e->rawCode, 'A');
    CHECK(!e->shift && !e->ctrl && !e->alt);
  } else {
    CHECK(!"wxEVT_KEY_DOWN A");
  }
  if (const WxRecord* e = FindEvent(wxEVT_CHAR, "viewport")) {
    CHECK_EQ(e->keyCode, 'a');
  } else {
    CHECK(!"wxEVT_CHAR a");
  }
  CHECK(FindEvent(wxEVT_KEY_UP, "viewport") != nullptr);
  CHECK(FindEvent(wxEVT_KEY_DOWN, "pushed") != nullptr);

  // Return: the key, then the character TranslateMessage makes; wx's WXK_RETURN for both.
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_RETURN, SDLK_RETURN, SDL_KMOD_NONE));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_RETURN, SDLK_RETURN, SDL_KMOD_NONE));
  if (const Message* ch = FindMessage(viewportHwnd, WM_CHAR)) {
    CHECK_EQ(ch->wParam, 13);
  } else {
    CHECK(!"WM_CHAR 13");
  }
  if (const WxRecord* e = FindEvent(wxEVT_CHAR, "viewport")) {
    CHECK_EQ(e->keyCode, WXK_RETURN);
  } else {
    CHECK(!"wxEVT_CHAR return");
  }

  // An arrow: no character, wx's wxEVT_CHAR comes from the key-down.
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_LEFT, SDLK_LEFT, SDL_KMOD_NONE));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_LEFT, SDLK_LEFT, SDL_KMOD_NONE));
  if (const Message* down = FindMessage(viewportHwnd, WM_KEYDOWN)) {
    CHECK_EQ(down->wParam, VK_LEFT);
    CHECK((down->lParam & (1 << 24)) != 0); // extended key
  }
  if (const WxRecord* e = FindEvent(wxEVT_KEY_DOWN, "viewport")) {
    CHECK_EQ(e->keyCode, WXK_LEFT);
    CHECK_EQ(e->rawCode, VK_LEFT);
  }
  if (const WxRecord* e = FindEvent(wxEVT_CHAR, "viewport")) {
    CHECK_EQ(e->keyCode, WXK_LEFT);
  } else {
    CHECK(!"wxEVT_CHAR left");
  }

  // Ctrl+C: VK_CONTROL, then 'C' with Ctrl, then the control character 3.
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_LCTRL, SDLK_LCTRL, SDL_KMOD_LCTRL));
  CHECK(::GetKeyState(VK_CONTROL) < 0);
  CHECK(::GetKeyState(VK_LCONTROL) < 0);
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_C, SDLK_C, SDL_KMOD_LCTRL));
  PushAndPump(TextEvent(frameId, "c")); // dropped: Ctrl+key makes its own character
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_C, SDLK_C, SDL_KMOD_LCTRL));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_LCTRL, SDLK_LCTRL, SDL_KMOD_NONE));
  CHECK(::GetKeyState(VK_CONTROL) >= 0);
  CHECK(FindMessage(viewportHwnd, WM_KEYDOWN) != nullptr && FindMessage(viewportHwnd, WM_KEYDOWN)->wParam == VK_CONTROL);
  CHECK_EQ(CountMessages(viewportHwnd, WM_CHAR), 1);
  if (const Message* ch = FindMessage(viewportHwnd, WM_CHAR)) {
    CHECK_EQ(ch->wParam, 3);
  }
  if (const WxRecord* e = FindEvent(wxEVT_KEY_DOWN, "viewport", 1)) {
    CHECK_EQ(e->keyCode, 'C');
    CHECK(e->ctrl);
    CHECK(!e->alt);
  } else {
    CHECK(!"wxEVT_KEY_DOWN Ctrl+C");
  }

  // Option is Alt: system keys with lParam bit 29.
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_LALT));
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_F9, SDLK_F9, SDL_KMOD_LALT));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_F9, SDLK_F9, SDL_KMOD_LALT));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_NONE));
  if (const Message* sys = FindMessage(viewportHwnd, WM_SYSKEYDOWN, 1)) {
    CHECK_EQ(sys->wParam, VK_F9);
    CHECK((sys->lParam & (1 << 29)) != 0);
  } else {
    CHECK(!"WM_SYSKEYDOWN F9");
  }
  if (const WxRecord* e = FindEvent(wxEVT_KEY_DOWN, "viewport", 1)) {
    CHECK_EQ(e->keyCode, WXK_F9);
    CHECK(e->alt);
  } else {
    CHECK(!"wxEVT_KEY_DOWN Alt+F9");
  }
  CHECK(::GetKeyState(VK_MENU) >= 0);

  // Shift: VK_SHIFT and VK_LSHIFT; a repeat sets lParam bit 30.
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, SDL_KMOD_LSHIFT));
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_D, SDLK_D, SDL_KMOD_LSHIFT));
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_D, SDLK_D, SDL_KMOD_LSHIFT, true));
  CHECK(::GetKeyState(VK_SHIFT) < 0);
  CHECK(::GetKeyState(VK_LSHIFT) < 0);
  CHECK(::GetKeyState(VK_RSHIFT) >= 0);
  if (const Message* repeat = FindMessage(viewportHwnd, WM_KEYDOWN, 2)) {
    CHECK_EQ(repeat->wParam, 'D');
    CHECK((repeat->lParam & (1 << 30)) != 0);
  } else {
    CHECK(!"repeat");
  }
  if (const WxRecord* e = FindEvent(wxEVT_KEY_DOWN, "viewport", 1)) {
    CHECK(e->shift);
  }
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_D, SDLK_D, SDL_KMOD_LSHIFT));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, SDL_KMOD_NONE));
  CHECK(::GetKeyState(VK_SHIFT) >= 0);

  // Text beyond ASCII: one WM_CHAR per UTF-16 unit.
  Clear();
  PushAndPump(TextEvent(frameId, "\xC3\xA9\xF0\x9F\x98\x80")); // U+00E9, U+1F600
  CHECK_EQ(CountMessages(viewportHwnd, WM_CHAR), 3);
  if (CountMessages(viewportHwnd, WM_CHAR) == 3) {
    CHECK_EQ(FindMessage(viewportHwnd, WM_CHAR, 0)->wParam, 0xE9);
    CHECK_EQ(FindMessage(viewportHwnd, WM_CHAR, 1)->wParam, 0xD83D);
    CHECK_EQ(FindMessage(viewportHwnd, WM_CHAR, 2)->wParam, 0xDE00);
  }

  // A key-down a handler takes (no Skip) swallows its character (wx's m_lastKeydownProcessed).
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_B, SDLK_B, SDL_KMOD_NONE));
  PushAndPump(TextEvent(frameId, "b"));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_B, SDLK_B, SDL_KMOD_NONE));
  CHECK(FindMessage(viewportHwnd, WM_CHAR) != nullptr);
  CHECK(FindEvent(wxEVT_CHAR, "viewport") == nullptr);

  // Layout: AZERTY's 'A' key (scancode Q) is VK 'A', scan code of the Q position.
  Clear();
  PushAndPump(KeyEvent(frameId, true, SDL_SCANCODE_Q, SDLK_A, SDL_KMOD_NONE));
  PushAndPump(KeyEvent(frameId, false, SDL_SCANCODE_Q, SDLK_A, SDL_KMOD_NONE));
  if (const Message* down = FindMessage(viewportHwnd, WM_KEYDOWN)) {
    CHECK_EQ(down->wParam, 'A');
    CHECK_EQ((down->lParam >> 16) & 0xFF, 0x10);
  }
  CHECK_EQ(::MapVirtualKeyW(VK_F1, MAPVK_VK_TO_VSC), 0x3B);
  CHECK_EQ(::MapVirtualKeyW(0x1E, MAPVK_VSC_TO_VK), 'A');

  // --- 7. Resize, move, maximize --------------------------------------------------------------
  std::printf("== resize and move\n");
  Clear();
  SDL_SetWindowSize(sdlFrame, static_cast<int>(std::lround(480 / density)), static_cast<int>(std::lround(320 / density)));
  SDL_SyncWindow(sdlFrame);
  PumpFor(100);
  frame->GetClientSize(&clientW, &clientH);
  CHECK_EQ(clientW, 480);
  CHECK_EQ(clientH, 320);
  const int sysSize = IndexOf(frameHwnd, WM_SYSCOMMAND);
  const int enter = IndexOf(frameHwnd, WM_ENTERSIZEMOVE);
  const int size = IndexOf(frameHwnd, WM_SIZE);
  const int exit = IndexOf(frameHwnd, WM_EXITSIZEMOVE);
  CHECK(sysSize >= 0 && enter > sysSize && size > enter && exit > size);
  if (sysSize >= 0) {
    CHECK_EQ(gMessages[static_cast<std::size_t>(sysSize)].wParam & 0xFFF0, SC_SIZE);
  }
  if (size >= 0) {
    CHECK_EQ(gMessages[static_cast<std::size_t>(size)].wParam, SIZE_RESTORED);
    CHECK_EQ(LOWORD(gMessages[static_cast<std::size_t>(size)].lParam), 480);
    CHECK_EQ(HIWORD(gMessages[static_cast<std::size_t>(size)].lParam), 320);
  }
  if (const WxRecord* e = FindEvent(wxEVT_SIZE, "frame")) {
    CHECK_EQ(e->width, 480);
  } else {
    CHECK(!"frame wxEVT_SIZE");
  }
  // The frame's default layout gives its only child the whole client area.
  CHECK(viewport->GetSize() == wxSize(480, 320));
  if (const WxRecord* e = FindEvent(wxEVT_SIZE, "viewport")) {
    CHECK_EQ(e->width, 480);
    CHECK_EQ(e->height, 320);
  } else {
    CHECK(!"viewport wxEVT_SIZE");
  }
  if (layer != nullptr) {
    const CGSizeLike drawable = Objc<CGSizeLike>(layer, "drawableSize");
    CHECK_EQ(std::lround(drawable.width), 480);
    CHECK_EQ(std::lround(drawable.height), 320);
  }
  // The size hints became the window's minimum.
  {
    int minW = 0;
    int minH = 0;
    SDL_GetWindowMinimumSize(sdlFrame, &minW, &minH);
    CHECK_EQ(minW, std::lround(320 / density));
  }

  Clear();
  int sx = 0;
  int sy = 0;
  SDL_GetWindowPosition(sdlFrame, &sx, &sy);
  SDL_SetWindowPosition(sdlFrame, sx + 13, sy + 7);
  SDL_SyncWindow(sdlFrame);
  PumpFor(100);
  if (show) {
    CHECK(FindMessage(frameHwnd, WM_MOVE) != nullptr);
    CHECK(FindEvent(wxEVT_MOVE, "frame") != nullptr);
  }
  // A move by the program (wx's SetSize) sends WM_SIZE synchronously, no sizing loop.
  Clear();
  frame->SetClientSize(400, 300);
  CHECK(FindMessage(frameHwnd, WM_SIZE) != nullptr);
  CHECK(FindMessage(frameHwnd, WM_ENTERSIZEMOVE) == nullptr);
  CHECK(viewport->GetClientSize() == wxSize(400, 300));

  // --- 8. Close button and Cmd+Q ---------------------------------------------------------------
  std::printf("== close\n");
  Clear();
  PushAndPump(WindowEvent(frameId, SDL_EVENT_WINDOW_CLOSE_REQUESTED));
  CHECK(FindMessage(frameHwnd, WM_SYSCOMMAND) != nullptr);
  CHECK(FindMessage(frameHwnd, WM_CLOSE) != nullptr);
  CHECK(FindEvent(wxEVT_CLOSE_WINDOW, "frame") != nullptr);
  CHECK(::IsWindow(frameHwnd) != FALSE); // vetoed: still there
  Clear();
  {
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    PushAndPump(e);
  }
  CHECK(FindMessage(frameHwnd, WM_CLOSE) != nullptr);
  CHECK(FindEvent(wxEVT_CLOSE_WINDOW, "frame") != nullptr);

  // --- 9. Cursor, monitors ---------------------------------------------------------------------
  std::printf("== cursor and monitors\n");
  {
    std::vector<std::uint32_t> pixels(32 * 32, 0xFFFF0000u);
    const HCURSOR cursor = faf_posix::shell::CreateColorCursor(pixels.data(), 32, 3, 4, density);
    CHECK(cursor != nullptr);
    const HCURSOR previous = ::SetCursor(cursor);
    CHECK(previous != nullptr);
    CHECK(::GetCursor() == cursor);
    CHECK_EQ(::ShowCursor(FALSE), -1);
    CHECK_EQ(::ShowCursor(TRUE), 0);
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    CHECK(::GetCursorInfo(&info) != FALSE);
    CHECK(info.flags == CURSOR_SHOWING);
    ::SetCursor(previous);
    CHECK(::DestroyCursor(cursor) != FALSE);
  }
  {
    const HMONITOR monitor = ::MonitorFromWindow(frameHwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    CHECK(::GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&info)) != FALSE);
    RECT frameRect{};
    ::GetWindowRect(frameHwnd, &frameRect);
    CHECK(frameRect.left >= info.rcMonitor.left && frameRect.right <= info.rcMonitor.right);
    CHECK(info.rcWork.bottom <= info.rcMonitor.bottom);
    const HMONITOR primary = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO primaryInfo{};
    primaryInfo.cbSize = sizeof(primaryInfo);
    CHECK(::GetMonitorInfoW(primary, &primaryInfo) != FALSE);
    CHECK_EQ(primaryInfo.rcMonitor.right - primaryInfo.rcMonitor.left, screenW);
    CHECK((primaryInfo.dwFlags & MONITORINFOF_PRIMARY) != 0);
    POINT cursor{};
    CHECK(::GetCursorPos(&cursor) != FALSE);
  }
  if (EnvOn("SHELL_TEST_WARP", false)) {
    POINT before{};
    ::GetCursorPos(&before);
    POINT target{0, 0};
    ::ClientToScreen(viewportHwnd, &target);
    target.x += 40;
    target.y += 30;
    CHECK(::SetCursorPos(target.x, target.y) != FALSE);
    POINT after{};
    ::GetCursorPos(&after);
    CHECK_EQ(after.x, target.x);
    CHECK_EQ(after.y, target.y);
    ::SetCursorPos(before.x, before.y);
  }
  if (EnvOn("SHELL_TEST_CLIPBOARD", false)) {
    char* const saved = SDL_GetClipboardText();
    CHECK(::OpenClipboard(frameHwnd) != FALSE);
    CHECK(::EmptyClipboard() != FALSE);
    const wchar_t text[] = L"faf shell é";
    HGLOBAL block = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(text));
    std::memcpy(::GlobalLock(block), text, sizeof(text));
    ::GlobalUnlock(block);
    CHECK(::SetClipboardData(CF_UNICODETEXT, block) != nullptr);
    CHECK(::CloseClipboard() != FALSE);
    CHECK(::IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE);
    CHECK(::OpenClipboard(frameHwnd) != FALSE);
    const HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    CHECK(data != nullptr && std::wcscmp(static_cast<const wchar_t*>(::GlobalLock(data)), text) == 0);
    ::CloseClipboard();
    SDL_SetClipboardText(saved != nullptr ? saved : "");
    SDL_free(saved);
  }

  // --- 10. Queue: posted messages, WM_QUIT, MsgWaitForMultipleObjectsEx -----------------------
  std::printf("== queue and waits\n");
  {
    CHECK(::PostMessageW(viewportHwnd, WM_USER + 5, 1, 2) != FALSE);
    CHECK(::PeekMessageW(&msg, nullptr, WM_USER, WM_USER + 10, PM_NOREMOVE) != FALSE);
    CHECK(msg.message == WM_USER + 5);
    CHECK(::PeekMessageW(&msg, nullptr, WM_USER, WM_USER + 10, PM_REMOVE) != FALSE);
    CHECK(::PeekMessageW(&msg, nullptr, WM_USER, WM_USER + 10, PM_REMOVE) == FALSE);
    ::PostQuitMessage(7);
    CHECK(::GetMessageW(&msg, nullptr, 0, 0) == FALSE);
    CHECK_EQ(msg.message, WM_QUIT);
    CHECK_EQ(msg.wParam, 7);

    const HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // A quiet 40 ms (input that arrives meanwhile - the real mouse - is taken and the wait
    // tried again).
    DWORD result = WAIT_FAILED;
    long long ms = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int attempt = 0; attempt < 20; ++attempt) {
      faf_posix::shell::PumpEvents();
      SDL_Event peek[8];
      const int pending = SDL_PeepEvents(peek, 8, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
      for (int i = 0; i < pending; ++i) {
        std::printf("   (pending SDL event 0x%x before the wait)\n", peek[i].type);
      }
      t0 = std::chrono::steady_clock::now();
      result = ::MsgWaitForMultipleObjectsEx(1, &event, 40, QS_ALLINPUT, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
      ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      if (result == WAIT_TIMEOUT) {
        break;
      }
      std::printf("   (input during the quiet wait, attempt %d)\n", attempt);
    }
    CHECK_EQ(result, WAIT_TIMEOUT);
    CHECK(ms >= 39 && ms < 200);

    std::thread signaller([event] {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      ::SetEvent(event);
    });
    t0 = std::chrono::steady_clock::now();
    result = ::MsgWaitForMultipleObjectsEx(1, &event, 2000, QS_ALLINPUT, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    signaller.join();
    CHECK_EQ(result, WAIT_OBJECT_0);
    CHECK(ms < 200);

    std::thread poster([viewportHwnd] {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      ::PostMessageW(viewportHwnd, WM_USER + 6, 0, 0);
    });
    t0 = std::chrono::steady_clock::now();
    result = ::MsgWaitForMultipleObjectsEx(1, &event, 2000, QS_ALLINPUT, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    poster.join();
    CHECK_EQ(result, WAIT_OBJECT_0 + 1);
    CHECK(ms < 200);
    faf_posix::shell::PumpEvents();

    SDL_Event input{};
    input.type = SDL_EVENT_MOUSE_MOTION;
    input.motion.windowID = frameId;
    input.motion.x = 5.0f;
    input.motion.y = 5.0f;
    SDL_PushEvent(&input);
    t0 = std::chrono::steady_clock::now();
    result = ::MsgWaitForMultipleObjectsEx(1, &event, 2000, QS_ALLINPUT, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK_EQ(result, WAIT_OBJECT_0 + 1);
    CHECK(ms < 50);
    faf_posix::shell::PumpEvents();
    CHECK(!faf_posix::shell::WaitForEvent(0) || faf_posix::shell::PumpEvents());
    ::CloseHandle(event);
  }

  // --- 11. wx application pump and idle --------------------------------------------------------
  std::printf("== wx pump and idle\n");
  {
    Clear();
    ::PostMessageW(viewportHwnd, WM_KEYDOWN, 'Z', 1 | (0x2C << 16));
    CHECK(app.Pending());
    app.Dispatch();
    CHECK(FindMessage(viewportHwnd, WM_KEYDOWN) != nullptr);
    CHECK(FindEvent(wxEVT_KEY_DOWN, "viewport") != nullptr);
    // Idle: wx's mouse-leave check runs from idle time.
    Clear();
    (void)app.ProcessIdle();
    POINT cursor{};
    ::GetCursorPos(&cursor);
    HWND under = ::WindowFromPoint(cursor);
    if (under != viewportHwnd) {
      CHECK(FindEvent(wxEVT_LEAVE_WINDOW, "viewport") != nullptr);
    }
  }

  CHECK(viewport->PopEventHandler(false) == pushed);
  delete pushed;

  // --- 12. Deactivation and teardown -----------------------------------------------------------
  std::printf("== deactivation and teardown\n");
  PushAndPump(WindowEvent(frameId, SDL_EVENT_WINDOW_FOCUS_GAINED));
  CHECK(::GetForegroundWindow() == frameHwnd);
  CHECK(::GetFocus() == viewportHwnd);
  Clear();
  PushAndPump(WindowEvent(frameId, SDL_EVENT_WINDOW_FOCUS_LOST));
  CHECK(::GetForegroundWindow() == nullptr);
  CHECK(::GetFocus() == nullptr);
  CHECK(FindMessage(frameHwnd, WM_ACTIVATEAPP) != nullptr && FindMessage(frameHwnd, WM_ACTIVATEAPP)->wParam == FALSE);
  CHECK(FindMessage(viewportHwnd, WM_KILLFOCUS) != nullptr);
  CHECK(FindEvent(wxEVT_KILL_FOCUS, "viewport") != nullptr);
  // Back: the focus returns to the viewport (DefWindowProc's WM_ACTIVATE).
  Clear();
  PushAndPump(WindowEvent(frameId, SDL_EVENT_WINDOW_FOCUS_GAINED));
  if (show) { // a hidden window's activation focuses the window itself
    CHECK(::GetFocus() == viewportHwnd);
    CHECK(FindEvent(wxEVT_SET_FOCUS, "viewport") != nullptr);
  }
  frame->Destroy();
  CHECK(::IsWindow(frameHwnd) != FALSE); // wx destroys top-level windows at idle time
  (void)app.ProcessIdle();
  CHECK(::IsWindow(frameHwnd) == FALSE);
  CHECK(::IsWindow(viewportHwnd) == FALSE);
  CHECK(wxFindWinFromHandle(reinterpret_cast<WXHWND>(viewportHwnd)) == nullptr);
  wxTheApp = nullptr;
  faf_posix::shell::Shutdown();
  CHECK(!faf_posix::shell::IsActive());
  CHECK_EQ(::GetSystemMetrics(SM_CXSCREEN), 1920);

  std::printf("shell_test: %d passed, %d failed\n", gPasses, gFailures);
  std::fflush(stdout);
  std::_Exit(gFailures == 0 ? 0 : 1);
}
