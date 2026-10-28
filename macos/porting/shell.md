# Porting notes: package "shell" (milestone 2)

The SDL3 window system behind the Win32 Window area and the wx stand-in: real windows,
input as Win32 messages and wx events, cursors, displays, the clipboard, and the
CAMetalLayer the Vulkan backend presents into.

## Result

- `Shell.h` implemented (`src/platform/posix/shell/`). `Init()` starts SDL video on the main
  thread; from then on the Window area's HWNDs are real and the wx stand-in's windows get
  them. Headless (no `Init()`, faf_replay) every function keeps its old answer.
- Test `macos/tests/shell/shell_test.cpp` (full engine linked): **243 passed, 0 failed**
  (windows shown briefly, without taking the focus); `SHELL_TEST_SHOW=0` (hidden): 233/0;
  `FAF_SHELL_HIGHDPI=0`: 243/0.
- Headless replay from my own tree (`build/m2-shell/faf_replay`, all changes in):
  `FAF_REPLAY_RESULT replay=27890640 beats=9717 mismatches=0 exit=end-of-replay`
  (73.8x real time).
- The vulkan package's `vk_shell_test` already ran this shell (frame + viewport child,
  `MetalLayer(viewport)`, resize): PASS on MoltenVK and KosmicKrisp (macos/porting/vulkan.md).

## Files (all mine)

| File | What |
|---|---|
| `shell/Shell.cpp` | Shell.h: Init/Shutdown/IsActive, `MetalLayer`, `PumpEvents`, `WaitForEvent`; shared state |
| `shell/ShellInternal.h` | The package's internal interface: the Window area's active-mode functions (`faf_posix::shell::win32::*`), `InputAvailable` (MsgWait), `CreateColorCursor` (gal cursor), `PixelDensity`, `RegisterWindowClass` |
| `shell/ShellState.h` | Private state (window table, queue, key state, cursor) |
| `shell/Windows.cpp` | The window table and every window function (create/destroy, geometry, show, z-order, styles, focus, capture, activation, `DefWindowProc`, metal-view placement, full screen) |
| `shell/Queue.cpp` | The main thread's message queue: Peek/Get/Post/Translate/Dispatch, WM_PAINT, WM_QUIT |
| `shell/Input.cpp` | SDL events -> messages (keys, text, mouse, wheel, window events), key state, cursors, clip |
| `shell/System.cpp` | Monitors, system metrics, work area, clipboard |
| `shell/MetalView.cpp` | NSView frame/hidden of a Metal view through the Objective-C runtime |
| `compat/Window.h`, `WinCompatWindow.cpp` | Each function forwards to the shell while it is active, else the old headless body. Added: `WNDCLASSW/EXW`, `RegisterClassW/ExW`, `UnregisterClassW`, `MINMAXINFO`, `GetMessagePos`, `WaitMessage`, `GetWindow` + `GW_*`, `IsChild`, `BringWindowToTop`, `ChildWindowFromPoint`, `CS_*` |
| `WinCompatSync.cpp` (`MsgWaitForMultipleObjects(Ex)` only) | Wakes on input on the main thread while the shell is active |
| `wx/wx/faf/{window,toplevel,app,gdi}.h`, new `wx/wx/faf/native.h`, `wx/wx/wx.h` | The wx stand-in with native windows (below) |
| `stubs/wx/WinAppPosix.cpp` | `WIN_AppExecute` pumps the queue (WinApp.cpp's `DispatchPendingMessage`); `WIN_CopyToClipboard` / `WIN_GetClipboardText` are WinApp.cpp's bodies |
| `stubs/render/gal/WindowsCursorPosix.cpp` | Real colour cursors |
| `macos/tests/shell/{shell_test.cpp,build_test.sh}` | The test |

## How the engine consumes input (research)

- **Windows**: `CScApp::CreateAppFrame` makes `WSupComFrame` (wxFrame, captioned and
  resizable when windowed, `wxNO_BORDER|wxSYSTEM_MENU` for the "fullscreen" head) and
  `REN_CreateGameViewport` -> `WRenViewport : WD3DViewport : wxWindow` as its child, sized to
  the frame's client area; `head.mHandle` = frame HWND, `head.mWindow` = viewport HWND;
  `viewport->SetFocus()`.
- **Keys and mouse go through wx events on the viewport**: `CUIManager::AddFrame(viewport,
  frame)` pushes `CUIKeyHandler` (key bindings: `m_rawCode` = the virtual key, plus
  `0x80000000/0x40000000/0x20000000` for Shift/Ctrl/Alt from the event's flags; Enter =
  chat, `~` = console; a key-down with lParam bit 30 and no repeat entry is dropped) and
  later the root `CMauiFrame`'s `CMauiWxEventMapper` (EVT_MOUSE_EVENTS incl. enter/leave
  and wheel, KEY_DOWN/UP, CHAR -> Maui events to the focus control / input capture / the
  control under the cursor; wheel positions are screen coordinates converted with the
  viewport's `ScreenToClient`). No DirectInput, no raw input, no keyboard polling besides:
- **`GetKeyState`**: `MAUI_KeyIsDown` (camera arrow-key pans, Insert/Delete spin, held
  modifiers) via `wxCharCodeWXToMSW`, only while `gal::WindowIsForeground()` (=
  `GetForegroundWindow()` is a head's frame or viewport).
- **Cursor**: `GetCursorPos`/`SetCursorPos` for the middle-drag camera "scrub" (warps the
  pointer back to an anchor each frame and accumulates the delta; anchor from
  `GetWindowRect(sMainWindow)`), screen-edge scrolling (`GetCursorPos` == 0 / head size - 1),
  `wxGetMousePosition` + `ScreenToClient` for the control under the cursor;
  `SetCapture`/`ReleaseCapture` (`eventMapper->mWindow->GetHWND()`) around Maui drags;
  `ClipCursor(GetWindowRect(frame))` when `lock_fullscreen_cursor_to_window`.
- **Window procedures**: `WD3DViewport::MSWWindowProc` takes WM_SETCURSOR(HTCLIENT) to show
  the device cursor (`d3d_WindowsCursor` for Vulkan: `D3D_InitCursor` -> gal
  `WindowsCursor`, `ShowCursor`), turns WM_XBUTTONDOWN/UP into WM_KEYDOWN/UP of
  VK_XBUTTON1/2 (bindable side buttons); `WSupComFrame::MSWWindowProc` /
  `MSWDefWindowProc` resize the viewport and re-init the device on WM_EXITSIZEMOVE after
  SC_SIZE, on WM_SIZE(SIZE_MAXIMIZED) and on the restore after it, clip/unclip the cursor on
  WM_ACTIVATEAPP, suppress SC_KEYMENU; `WSupComFrame::OnCloseWindow` (WM_CLOSE) opens the
  escape dialog; `OnMove` saves Windows.Main.x/y. `WRenViewport` focuses a head's window on
  enter/leave with two heads.
- No IME handling, no keyboard hook on POSIX (WinApp.cpp's WH_KEYBOARD_LL is Windows-only).

## Design

**Coordinates.** Everything Win32 code sees is in drawable pixels. Client sizes are the SDL
window's drawable (`SDL_GetWindowSizeInPixels`; `SDL_WINDOW_HIGH_PIXEL_DENSITY`). Screen
coordinates are SDL's global points times the **primary display's** pixel density (one scale
for the whole desktop, so `GetCursorPos`, `GetWindowRect`, `ClientToScreen`, monitor rects
agree). The non-client area is AppKit's title bar (`SDL_GetWindowBordersSize`, 28 pt =
56 px on Retina); `AdjustWindowRectEx` adds it for `WS_CAPTION`. `FAF_SHELL_HIGHDPI=0`
creates windows at one pixel per point (the engine's 1024x768 is then 1024x768 points).

**Windows.** A table of records; HWNDs are opaque values from 0x00100000. A top-level HWND
(no `WS_CHILD`) is an SDL window created hidden with `SDL_WINDOW_METAL` (+ RESIZABLE for
`WS_THICKFRAME`, BORDERLESS without a full `WS_CAPTION`, ALWAYS_ON_TOP for
`WS_EX_TOPMOST`), text input on. A child HWND is a rectangle in its parent's client area
(no non-client area), with visibility, enabled state, z-order for hit testing, focus and
its own procedure. Window classes (`RegisterClassW`) give the procedure, the class cursor and
`CS_DBLCLKS`. Sizes passed to `CreateWindowEx`/`SetWindowPos`/`MoveWindow` for top-level
windows include the frame, as on Windows. The table lock is never held while a window
procedure runs.

**MetalLayer(hwnd).** `SDL_Metal_CreateView` on the top-level SDL window, once per HWND. For
a top-level window, or a child that exactly fills its top-level's client area (the game's
viewport), the view fills the content view and follows its size (SDL keeps the layer's
`drawableSize`/`contentsScale` in step). Any other child gets an explicit frame over its
rectangle (re-placed when it or an ancestor moves, hidden with it). Verified:
`drawableSize` 400x300 for a 400x300 px viewport at `contentsScale` 2, an inset child at
(10,20) 100x60 px is a view at (5,10) 50x30 pt with a 100x60 drawable.

**Messages.** Sent as Windows sends them: geometry changes (`SetWindowPos` etc.) send
`WM_MOVE`/`WM_SIZE` synchronously; a size change made outside the program (the user, or the
display's scale changing) runs the sizing sequence `WM_SYSCOMMAND(SC_SIZE)`,
`WM_ENTERSIZEMOVE`, `WM_SIZE(SIZE_RESTORED)`, `WM_EXITSIZEMOVE` - the WSupComFrame resizes
its viewport and re-inits the device on WM_EXITSIZEMOVE; maximize/restore/minimize send
`WM_SIZE` with `SIZE_MAXIMIZED`/`SIZE_RESTORED`/`SIZE_MINIMIZED` (a captioned window in a
full-screen space counts as maximized). Activation: `WM_ACTIVATEAPP`, `WM_NCACTIVATE`,
`WM_ACTIVATE` (DefWindowProc puts the focus back on the child that had it), deactivation the
reverse plus `WM_KILLFOCUS`; focus `WM_KILLFOCUS`/`WM_SETFOCUS`; capture `WM_CAPTURECHANGED`.
Input is queued: SDL events are taken one at a time while the queue is empty, so a sent
message keeps its place among the input. `PeekMessage` returns queued/posted messages in
order, `WM_QUIT` when nothing else is queued, then `WM_PAINT` for a visible window that
needs it (validated as it is taken: the stand-in's paint DCs do not call BeginPaint). Mouse
moves are coalesced. `WM_SETCURSOR(HTCLIENT)` is sent as a mouse move or button-down is
taken (not while the mouse is captured).

**Keys.** `WM_KEYDOWN/UP`; `WM_SYSKEYDOWN/UP` while Option (= Alt, VK_MENU) is down without
Ctrl, and for F10. lParam: repeat 1, PC set-1 scan code from SDL's scancode, extended bit,
Alt bit 29, previous-state bit 30, transition bit 31. Virtual keys: letters follow the
layout (SDL keycode), everything else the key's position (digits, punctuation as US
VK_OEM_*, numpad VK_NUMPAD*). Ctrl is Ctrl, Option is Alt, Command is VK_LWIN/VK_RWIN.
`GetKeyState` follows the messages as they are taken (L/R modifiers too, toggle bit);
`GetAsyncKeyState` the device. Characters: SDL text input becomes `WM_CHAR`, one per
UTF-16 unit, after its key; `TranslateMessage` adds the characters text input does not
produce (Backspace 8, Tab 9, Enter 13, Esc 27, Ctrl+letter 1..26, Ctrl+[\] 27..29); text
typed with Ctrl down is dropped (Ctrl+key makes its own character).

**Mouse.** To the deepest visible, enabled child under the cursor, or to the capture window
(any position, its client coordinates); outside the client area without a capture nothing
is sent (Windows sends it to the other application). `MK_*` flags from the buttons and
Shift/Ctrl. Double clicks: AppKit's click count, even counts are `WM_xBUTTONDBLCLK` for
classes with `CS_DBLCLKS`. `WM_XBUTTON*` with XBUTTON1/2. `WM_MOUSEWHEEL` to the focus window
with screen coordinates, WHEEL_DELTA units, fractional trackpad steps accumulated, and the
physical direction (macOS natural scrolling undone: the wheel zooms as on Windows).

**Close.** The close button and Cmd+Q (SDL_EVENT_QUIT; also Alt+F4) post
`WM_SYSCOMMAND(SC_CLOSE)` to the window (Cmd+Q: the active one); DefWindowProc sends
`WM_CLOSE`; the wx stand-in turns it into `Close()` (WSupComFrame shows the escape dialog).

**Cursor.** `SetCursor`/`ShowCursor` drive SDL (system cursors for the IDC_ ids, colour
cursors from `CreateColorCursor`); the visible state is `cursor != 0 && count >= 0`.
`WindowsCursorPosix.cpp` reads the 32x32 cursor texture like the Windows file and makes an
SDL colour cursor whose base image is 16x16 pt with the 32x32 pixels as the Retina
representation (same size relative to the pixel-sized UI), hotspot scaled. `SetCursorPos`
warps (`SDL_WarpMouseGlobal`), `GetCursorPos` reads the live position, `ClipCursor` becomes
`SDL_SetWindowMouseRect` on the window it lies on.

**Displays.** Monitors are SDL's displays, primary first; `GetMonitorInfo` rcMonitor/rcWork,
`GetSystemMetrics` screen/virtual screen/monitor count/caption/maximized/full-screen sizes,
`SPI_GETWORKAREA`; everything else keeps the headless table. A borderless top-level window
that covers its whole monitor (the game's "fullscreen" frame at the display's resolution)
goes into SDL desktop full screen, and out again when it no longer covers it or gets a
caption.

**Clipboard.** `OpenClipboard`..`CloseClipboard` over the system clipboard's text:
`CF_UNICODETEXT` (wchar_t, as in-memory wide text is on POSIX) and `CF_TEXT` (UTF-8) in
GlobalAlloc memory.

**MsgWaitForMultipleObjects(Ex)** (Sync area, my two functions): on the main thread with the
shell active and a wake mask with input, the object wait runs in 2 ms slices with
`InputAvailable()` (SDL pumped) between them; objects and APCs end the wait at once, input
within 2 ms; a posted message signals the queue event (`faf_posixSetThreadMessageEvent`,
result `WAIT_OBJECT_0 + count`); a stale signal of that event with nothing queued is not
reported. AppKit hands events to the process only on the main thread's run loop, so the
wait cannot block in the run loop and on the objects at once; this is the cheapest correct
mix. Any input counts (as with `MWMO_INPUTAVAILABLE`, which the engine passes).

**WIN_AppExecute.** Same order of calls as WinApp.cpp: before-events stage, then one message
(`wxApp::Pending` peeks the queue; WinApp.cpp's `DispatchPendingMessage`: take it, WM_QUIT
ends the loop, else `wxApp::DoMessage`), else `wxApp::ProcessIdle`, then `IWinApp::Main`, the
before-wait stage and `MsgWaitEx` (wakes on input). Headless the queue is empty and the loop
is what it was.

### The wx stand-in with native windows (wx 2.4.2 MSW behaviour)

- Creation: `wxWindow::Create` and the top-level `Create`s call `CreateWindowExW` with wx's
  classes (`wxWindowClass`, `wxWindowClassNR`: `CS_DBLCLKS`, arrow cursor, wx's window
  procedure) and `MSWGetStyle` (wxTopLevelWindowMSW's mapping: caption, thick frame, popup
  for borderless, min/max boxes, topmost; children `WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE`);
  default top-level position/size are `CW_USEDEFAULT` (centred, 800x600 pt). The HWND <->
  wxWindow map is wx's `wxFindWinFromHandle`. `~wxWindow` unmaps then destroys.
- Geometry is the native window's (`DoGetPosition/Size/ClientSize`, `DoSetSize` ->
  `MoveWindow`, `DoSetClientSize`'s measure-the-frame loop), `Show` (`wxTopLevelWindowMSW`:
  maximize-on-show, bring to top, wxEVT_ACTIVATE), `Maximize/Iconize/Restore/IsMaximized/
  IsIconized`, `SetTitle`, `Enable`, `SetFocus/FindFocus`, `CaptureMouse/ReleaseMouse/
  GetCapture`, `ClientToScreen/ScreenToClient`, `WarpPointer`, `Refresh/Update`,
  `Raise/Lower`, `SetWindowStyleFlag` (frame change), `SetSizeHints` (min/max size through
  `WM_GETMINMAXINFO`), `wxGetMousePosition`, `wxFindWindowAtPoint`, `wxDisplaySize`.
- `wxWindow::MSWWindowProc` (wx/faf/native.h) is wxWindowMSW's for the messages the shell
  sends: WM_SIZE (wxSizeEvent; frames: wxIconizeEvent; unhandled -> the top-level's layout,
  which gives a frame's only child the client area), WM_MOVE, WM_ACTIVATE, WM_SETFOCUS/
  KILLFOCUS, WM_SHOWWINDOW, WM_PAINT, WM_CLOSE (`Close()`, never DefWindowProc's destroy),
  WM_ACTIVATEAPP (`wxApp::SetActive`), WM_GETMINMAXINFO, WM_SETCURSOR (wxEVT_SET_CURSOR),
  mouse (enter on first move, `wxUSE_MOUSEEVENT_HACK`, focus on left click), WM_MOUSEWHEEL
  (screen coordinates, rotation, WHEEL_DELTA, lines per action), keys (`wxCharCodeMSWToWX`,
  m_rawCode/m_rawFlags, Alt from lParam, Shift/Ctrl from GetKeyState, the char-from-keydown
  path for keys without a character, the "not interesting" modifier keys, a handled
  key-down swallowing its WM_CHAR, AltGr chars without modifiers). Events go to
  `GetEventHandler()`, so pushed handlers (the UI's CMauiWxEventMapper / CUIKeyHandler on
  the viewport) see them first; `WD3DViewport`/`WSupComFrame` overrides run before all this
  as on Windows.
- `wxApp`: `Pending/Dispatch/DoMessage/ProcessMessage` (accelerators up to the top-level,
  then the parents' `MSWProcessMessage`) and `ProcessIdle` (wxIdleEvent to the app; default:
  pending events, pending deletes, idle events to every window, whose default is wx's
  mouse-leave check, `wxEVT_LEAVE_WINDOW` from idle time).

## Tests

`macos/tests/shell/build_test.sh` compiles `shell_test.cpp` with the engine's flags from
`build/m2-shell/compile_commands.json` and links it with the engine's objects as faf_replay
is (the runner's objects replaced), then runs it under a 90 s alarm (plus a 60 s in-process
watchdog). The process is a background app (`SDL_HINT_MAC_BACKGROUND_APP`) and shows its
windows without activation (`SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN=0`, which the shell's
`BringWindowToTop` also honours); focus changes are injected.

| Area | Checks |
|---|---|
| Headless before Init | wx frame/child have no HWND and keep their geometry; 1920 px screen; no layer, no messages, `CreateWindowExW` null, key state up |
| Frame + viewport | HWNDs, parent/root/IsChild/WS_CHILD, client 400x300 px at density 2, window rect width 400 and title bar 56 px, viewport client 400x300 at (0,0), ClientToScreen equal origins and round trip |
| MetalLayer | viewport layer drawable 400x300, contentsScale 2, one layer per window; inset child view frame and drawable, follows SetSize; destroyed with the window |
| Show / paint | visible, wxEVT_PAINT on show, Refresh -> one WM_PAINT, validated |
| Focus / activation | SetFocus -> WM_SETFOCUS + wxEVT_SET_FOCUS, FindFocus; focus-gained -> WM_ACTIVATEAPP before WM_ACTIVATE(WA_ACTIVE), wxEVT_ACTIVATE, foreground = frame; focus-lost -> foreground null, WM_ACTIVATEAPP(FALSE), WM_KILLFOCUS; regain -> focus back on the viewport |
| Mouse | WM_SETCURSOR before WM_MOUSEMOVE, pixel coordinates (50.5 pt -> 101 px), enter + motion events, pushed handler first, class cursor applied; L down/up/double click with MK_ flags and wx events; R, M (GetKeyState VK_MBUTTON), X2 (HIWORD XBUTTON2); outside without capture dropped, with capture delivered in capture-window coordinates; WM_CAPTURECHANGED |
| Wheel | +120 with screen coordinates, natural-scrolling flip -> -120, trackpad 0.05 steps -> 6 each, wxEVT_MOUSEWHEEL rotation/position |
| Keys | A: WM_KEYDOWN 'A' scan 0x1E repeat 1, WM_CHAR 'a' after it, WM_KEYUP bits 30/31, wx KEY_DOWN/CHAR/KEY_UP, GetKeyState/GetAsyncKeyState; Return -> WM_CHAR 13 / WXK_RETURN; Left -> extended, wxEVT_CHAR WXK_LEFT; Ctrl+C -> VK_CONTROL, VK_LCONTROL, one WM_CHAR 3, ctrl flag; Option+F9 -> WM_SYSKEYDOWN bit 29, alt flag; Shift L/R state, repeat bit 30; UTF-16 surrogates; handled key-down swallows WM_CHAR; AZERTY A key; MapVirtualKey |
| Resize / move | SDL resize -> SC_SIZE, ENTERSIZEMOVE, WM_SIZE(480x320), EXITSIZEMOVE in order, wx size events, viewport relaid out to 480x320, layer drawable 480x320, size hints became the SDL minimum; move -> WM_MOVE/wxMoveEvent; program SetClientSize -> WM_SIZE without the sizing loop |
| Close / quit | close button and SDL_EVENT_QUIT -> WM_SYSCOMMAND, WM_CLOSE, wxCloseEvent (vetoed: window stays) |
| Cursor / monitors | colour cursor, SetCursor/GetCursor, ShowCursor counts, GetCursorInfo; monitor of the window contains it, work area, primary monitor = SM_CXSCREEN (6016 px here) |
| Queue / waits | PostMessage + filtered PeekMessage, PostQuitMessage -> GetMessage FALSE with the code; MsgWait: quiet 40 ms -> WAIT_TIMEOUT, event set from a thread -> WAIT_OBJECT_0 (< 200 ms), PostMessage from a thread -> input, SDL input -> input (< 50 ms) |
| wx pump / idle | posted WM_KEYDOWN through `wxApp::Pending/Dispatch` -> wx KEY_DOWN; ProcessIdle -> wxEVT_LEAVE_WINDOW when the real cursor is elsewhere |
| Teardown | Destroy() deferred to idle, HWNDs gone, map entries gone, Shutdown -> headless answers again |

Not run (they touch the user's session; opt-in): `SHELL_TEST_WARP=1` (SetCursorPos warp
and read-back), `SHELL_TEST_CLIPBOARD=1` (clipboard round trip; restores text only).
Not tested: borderless full screen (would take over the screen), a real user drag of the
frame (simulated with SDL_SetWindowSize: same SDL events), multiple monitors, a display
scale change.

## Stubs and open issues

- **Cmd as Ctrl is not mapped** (as asked): macOS shortcuts like Cmd+V do nothing in the
  game; Ctrl+V is the game's paste.
- **Option + letter**: Windows sends WM_SYSCHAR with the plain letter; here AppKit's
  composed character (Option layer: å, @ on some layouts) arrives as WM_CHAR. The key
  bindings (Alt-x) work from the key messages.
- **IME**: committed text arrives as WM_CHAR; no WM_IME_* composition messages.
- **Mixed-DPI monitors**: screen coordinates use the primary display's scale; a window on a
  display with another scale has correct client pixels, and screen<->client conversion
  scales by the ratio (rounding).
- **Child windows**: no non-client area (borders/scroll bars of children are ignored);
  overlapping children's Metal views stack in creation order, not SetWindowPos z-order.
- **wx controls are not drawn**: a shown dialog (log window, Lua debugger) is an empty
  window. Message boxes still go to stderr (no modal dialog, so unattended runs never block).
- No `SetTimer`/WM_TIMER; hooks (`SetWindowsHookEx`) are still accepted and never called.
- Input latency in `MsgWaitForMultipleObjectsEx`: up to 2 ms (see above).
- `WD3DViewport`'s WM_SETCURSOR handler calls `ShowCursor(TRUE)` on every mouse move, so the
  display count climbs (as on Windows); hiding loops back down below zero.
- Edge-of-screen scrolling (`ui_ScreenEdgeScrollView`) compares GetCursorPos with 0 and the
  head size: right only for a full-screen frame at the screen origin, as on Windows.
- `wx.md`'s "Nothing is displayed" paragraph describes the headless mode only now.

## Requests

- **Game (faf_game main, bring-up)**: call `faf_posix::shell::Init()` on the main thread
  before `WIN_AppExecute` (before `wxEntry`/`CScApp::Init` create windows) and
  `faf_posix::shell::Shutdown()` after it; the headless runner must not. With default
  prefs (`primary_adapter = 1024,768,60`) CScApp builds its borderless "fullscreen" frame
  of 1024x768 px - 512x384 pt on Retina, centred; pass `/windowed 1600 1000` (CScApp's
  windowed branch: a captioned, resizable window, client in pixels) to watch a replay in a
  window, or set `FAF_SHELL_HIGHDPI=0` for point-sized windows; a borderless frame the size
  of the display goes into desktop full screen.
- **Render/vulkan**: as requested in vulkan.md, the frame now gets WM_SIZE on a window resize
  and the sizing sequence (ending in WM_EXITSIZEMOVE, which makes WSupComFrame resize the
  viewport and call InitContext) also when the display's scale changes. Device teardown
  should come before `DestroyWindow` of the head windows (MoltenVK retains the layer, so the
  other order is safe but presents into nothing).
- **Lead**: nothing in `macos/CMakeLists.txt` (shell/*.cpp are globbed into the engine; the
  wx stand-in includes `platform/posix/shell/Shell.h` and `ShellInternal.h` through the
  existing `src` include path). `macos/porting/wx.md` could point to this file for the
  native-window mode.
