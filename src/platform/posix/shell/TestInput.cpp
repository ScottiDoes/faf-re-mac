// Synthetic input for unattended tests (macos/porting/bringup.md, level 5). Debug only and
// off by default: nothing here runs unless the environment variable FAF_SHELL_TEST_INPUT is
// set when Init() runs.
//
// FAF_SHELL_TEST_INPUT is a list of steps separated by ';', each "<frame>:<action> [args]".
// <frame> counts the WM_PAINT messages the main thread has taken (one per presented game
// frame once the device is up); a step runs when its frame is reached. Coordinates are client
// pixels of the first top-level window (what the engine sees). Actions:
//   move X Y              the mouse moves to (X, Y)
//   rel DX DY             the mouse moves by (DX, DY) from where the pointer is now, as a
//                         physical mouse does: after the game warped the pointer
//                         (SetCursorPos: the middle-drag camera re-centres it every frame)
//                         the motion starts from the warped position
//   down X Y B [C] / up X Y B [C]  button B (1 left, 2 middle, 3 right) pressed / released
//                         at (X, Y) ("here" in place of X Y: where the pointer is); C is
//                         AppKit's click count (2: the second click of a double click)
//   wheel X Y N           the mouse moves to (X, Y), then the wheel turns N notches (N > 0:
//                         away from the user, as a Windows WHEEL_DELTA of +120 per notch)
//   key NAME [down|up]    a key (an SDL key name, spaces allowed: "Up", "A", "Return", "F1",
//                         "Left Shift", "Left Ctrl", "Left Alt", "Left GUI" (Command), ...);
//                         the modifier keys held by the script are in each key event's
//                         modifier state, as SDL reports them
//   text WORD             typed text (SDL text input: WM_CHAR), the rest of the step
//   focus lost|gained     the first window loses / regains the keyboard focus, as when the
//                         user switches to another application (Cmd+Tab) and back; as SDL
//                         does, the keys the script holds are released first
//   quit                  SDL's quit request (Cmd+Q, the application menu's Quit)
//   close                 the window's close button
//   shot NAME             captures the next rendered frame into $FAF_SHELL_TEST_SHOTS/<frame>-NAME/
//                         (faf_game: the engine's dump_Frame, a BMP; the folder path must
//                         not contain a space, the console splits arguments at spaces)
//   log TEXT              writes TEXT to stderr
// Every step is posted with SDL_PushEvent, so it takes the shell's normal path (Input.cpp)
// into Win32 messages and wx events. While the script is active:
//  - the cursor is virtual: GetCursorPos and the other cursor reads return the position the
//    script set, and SetCursorPos (the middle-drag camera's pointer warp) moves only that
//    position, never the user's real pointer;
//  - mouse and keyboard events that did not come from the script are dropped, so the
//    user's own input cannot disturb a test; so is the window system's focus loss (another
//    program's window coming to the front), so the window keeps the keyboard.
// Each step is logged to stderr as "[shell-test] frame N: <step>".

#include "platform/posix/shell/ShellState.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace faf_posix::shell::detail
{
  namespace
  {
    // The `which` id of the script's mouse and keyboard events.
    constexpr SDL_MouseID kTestMouse = 0x7E57u;
    constexpr SDL_KeyboardID kTestKeyboard = 0x7E57u;
    // data1 of the script's window events (focus, close).
    constexpr Sint32 kTestWindowEvent = 0x7E57;

    struct Step
    {
      unsigned long frame = 0;
      std::string action;
      std::vector<std::string> args;
      std::string text;
    };

    struct TestState
    {
      bool active = false;
      std::vector<Step> steps;
      std::size_t next = 0;
      unsigned long frame = 0;
      float cursorX = 0.0f; // global points
      float cursorY = 0.0f;
      std::uint32_t buttons = 0; // SDL_BUTTON_MASK bits
      SDL_Keymod mods = SDL_KMOD_NONE; // modifier keys the script holds
      std::vector<std::pair<SDL_Scancode, SDL_Keycode>> held; // keys the script holds down
      std::vector<std::unique_ptr<char[]>> texts; // text events' strings (SDL keeps the pointer)
      int pendingTexts = 0; // pushed text events not yet taken (text events carry no source id)
    };

    TestState& T() noexcept
    {
      static TestState state;
      return state;
    }

    TestShotHandler sShotHandler = nullptr;

    void Shot(const Step& step) noexcept
    {
      const char* const root = std::getenv("FAF_SHELL_TEST_SHOTS");
      if (sShotHandler == nullptr || root == nullptr || root[0] == '\0' || std::strchr(root, ' ') != nullptr) {
        std::fprintf(stderr, "[shell-test] shot skipped: %s\n",
                     sShotHandler == nullptr ? "no capture in this program" : "FAF_SHELL_TEST_SHOTS unset or has a space");
        return;
      }
      std::string name = step.args.empty() ? std::string("shot") : step.args[0];
      for (char& c : name) {
        if (c == '/' || c == ' ' || c == '\\') {
          c = '_';
        }
      }
      const std::string directory = std::string(root) + "/" + std::to_string(T().frame) + "-" + name;
      (void)::mkdir(root, 0755);
      (void)::mkdir(directory.c_str(), 0755);
      sShotHandler(directory.c_str());
    }

    std::string Trim(const std::string& text)
    {
      const std::size_t begin = text.find_first_not_of(" \t\r\n");
      if (begin == std::string::npos) {
        return {};
      }
      const std::size_t end = text.find_last_not_of(" \t\r\n");
      return text.substr(begin, end - begin + 1);
    }

    // The first top-level window: its SDL id, client origin (global points) and density.
    bool MainWindow(SDL_WindowID* id, float* originX, float* originY, float* density) noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      for (const HWND handle : s.topLevels) {
        const WindowRecord* const record = Find(handle);
        if (record != nullptr && record->sdl != nullptr && record->visible) {
          *id = record->sdlId;
          *originX = static_cast<float>(record->screenX);
          *originY = static_cast<float>(record->screenY);
          *density = DensityOf(record);
          return true;
        }
      }
      return false;
    }

    float Number(const Step& step, std::size_t index) noexcept
    {
      return index < step.args.size() ? static_cast<float>(std::atof(step.args[index].c_str())) : 0.0f;
    }

    Uint8 Button(const Step& step, std::size_t index) noexcept
    {
      switch (static_cast<int>(Number(step, index))) {
        case 2: return SDL_BUTTON_MIDDLE;
        case 3: return SDL_BUTTON_RIGHT;
        default: return SDL_BUTTON_LEFT;
      }
    }

    void Run(const Step& step) noexcept
    {
      TestState& t = T();
      std::fprintf(stderr, "[shell-test] frame %lu: %s\n", t.frame, step.text.c_str());
      if (step.action == "log") {
        return;
      }
      if (step.action == "shot") {
        Shot(step);
        return;
      }
      SDL_WindowID window = 0;
      float originX = 0.0f;
      float originY = 0.0f;
      float density = 1.0f;
      if (!MainWindow(&window, &originX, &originY, &density)) {
        std::fprintf(stderr, "[shell-test] no visible window; step skipped\n");
        return;
      }
      const auto moveTo = [&](float px, float py, float* x, float* y) {
        *x = px / density;
        *y = py / density;
        t.cursorX = originX + *x;
        t.cursorY = originY + *y;
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.timestamp = SDL_GetTicksNS();
        event.motion.windowID = window;
        event.motion.which = kTestMouse;
        event.motion.state = t.buttons;
        event.motion.x = *x;
        event.motion.y = *y;
        (void)SDL_PushEvent(&event);
      };

      float x = 0.0f;
      float y = 0.0f;
      if (step.action == "move") {
        moveTo(Number(step, 0), Number(step, 1), &x, &y);
      } else if (step.action == "rel") {
        // From the pointer's current position (client pixels), wherever the game put it.
        const float dx = Number(step, 0) / density;
        const float dy = Number(step, 1) / density;
        t.cursorX += dx;
        t.cursorY += dy;
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.timestamp = SDL_GetTicksNS();
        event.motion.windowID = window;
        event.motion.which = kTestMouse;
        event.motion.state = t.buttons;
        event.motion.x = t.cursorX - originX;
        event.motion.y = t.cursorY - originY;
        event.motion.xrel = dx;
        event.motion.yrel = dy;
        (void)SDL_PushEvent(&event);
      } else if (step.action == "down" || step.action == "up") {
        // "here" in place of X Y: where the pointer is now (after relative motion and warps).
        const bool here = !step.args.empty() && step.args[0] == "here";
        if (here) {
          x = t.cursorX - originX;
          y = t.cursorY - originY;
        } else {
          moveTo(Number(step, 0), Number(step, 1), &x, &y);
        }
        const std::size_t argBase = here ? 1 : 2;
        const bool down = step.action == "down";
        const Uint8 button = Button(step, argBase);
        if (down) {
          t.buttons |= SDL_BUTTON_MASK(button);
        } else {
          t.buttons &= ~SDL_BUTTON_MASK(button);
        }
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.timestamp = SDL_GetTicksNS();
        event.button.windowID = window;
        event.button.which = kTestMouse;
        event.button.button = button;
        event.button.down = down;
        event.button.clicks = static_cast<Uint8>(step.args.size() > argBase + 1 ? std::max(1, std::atoi(step.args[argBase + 1].c_str())) : 1);
        event.button.x = x;
        event.button.y = y;
        (void)SDL_PushEvent(&event);
      } else if (step.action == "wheel") {
        moveTo(Number(step, 0), Number(step, 1), &x, &y);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.timestamp = SDL_GetTicksNS();
        event.wheel.windowID = window;
        event.wheel.which = kTestMouse;
        event.wheel.x = 0.0f;
        event.wheel.y = Number(step, 2);
        event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        event.wheel.mouse_x = x;
        event.wheel.mouse_y = y;
        event.wheel.integer_y = static_cast<Sint32>(event.wheel.y);
        (void)SDL_PushEvent(&event);
      } else if (step.action == "key") {
        // The key's name is every word but a trailing "down" / "up" ("Left Shift down").
        std::vector<std::string> words = step.args;
        bool down = true;
        if (!words.empty() && (words.back() == "down" || words.back() == "up")) {
          down = words.back() == "down";
          words.pop_back();
        }
        std::string name;
        for (const std::string& word : words) {
          name += (name.empty() ? "" : " ") + word;
        }
        const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key == SDLK_UNKNOWN) {
          std::fprintf(stderr, "[shell-test] unknown key name '%s'; step skipped\n", name.c_str());
          return;
        }
        SDL_Keymod mod = SDL_KMOD_NONE;
        const SDL_Scancode scancode = SDL_GetScancodeFromKey(key, &mod);
        // SDL's modifier state includes the modifier key's own change (a Shift key-down
        // already reports Shift).
        SDL_Keymod bit = SDL_KMOD_NONE;
        switch (scancode) {
          case SDL_SCANCODE_LSHIFT: bit = SDL_KMOD_LSHIFT; break;
          case SDL_SCANCODE_RSHIFT: bit = SDL_KMOD_RSHIFT; break;
          case SDL_SCANCODE_LCTRL: bit = SDL_KMOD_LCTRL; break;
          case SDL_SCANCODE_RCTRL: bit = SDL_KMOD_RCTRL; break;
          case SDL_SCANCODE_LALT: bit = SDL_KMOD_LALT; break;
          case SDL_SCANCODE_RALT: bit = SDL_KMOD_RALT; break;
          case SDL_SCANCODE_LGUI: bit = SDL_KMOD_LGUI; break;
          case SDL_SCANCODE_RGUI: bit = SDL_KMOD_RGUI; break;
          default: break;
        }
        t.mods = down ? static_cast<SDL_Keymod>(t.mods | bit) : static_cast<SDL_Keymod>(t.mods & ~bit);
        t.held.erase(std::remove_if(t.held.begin(), t.held.end(), [&](const auto& k) { return k.first == scancode; }),
                     t.held.end());
        if (down) {
          t.held.emplace_back(scancode, key);
        }
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.timestamp = SDL_GetTicksNS();
        event.key.windowID = window;
        event.key.which = kTestKeyboard;
        event.key.scancode = scancode;
        event.key.key = key;
        event.key.mod = t.mods;
        event.key.down = down;
        (void)SDL_PushEvent(&event);
      } else if (step.action == "focus" || step.action == "close") {
        const bool lost = step.action == "focus" && !step.args.empty() && step.args[0] == "lost";
        if (lost) {
          // What SDL does when the window loses the keyboard focus (SDL_ResetKeyboard): every
          // key still down is released, before the focus event.
          for (auto it = t.held.rbegin(); it != t.held.rend(); ++it) {
            SDL_Event release{};
            release.type = SDL_EVENT_KEY_UP;
            release.key.timestamp = SDL_GetTicksNS();
            release.key.windowID = window;
            release.key.which = kTestKeyboard;
            release.key.scancode = it->first;
            release.key.key = it->second;
            release.key.mod = SDL_KMOD_NONE;
            release.key.down = false;
            (void)SDL_PushEvent(&release);
          }
          t.held.clear();
          t.mods = SDL_KMOD_NONE;
        }
        SDL_Event event{};
        if (step.action == "close") {
          event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        } else {
          event.type = lost ? SDL_EVENT_WINDOW_FOCUS_LOST : SDL_EVENT_WINDOW_FOCUS_GAINED;
        }
        event.window.timestamp = SDL_GetTicksNS();
        event.window.windowID = window;
        event.window.data1 = kTestWindowEvent;
        (void)SDL_PushEvent(&event);
      } else if (step.action == "quit") {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        event.quit.timestamp = SDL_GetTicksNS();
        (void)SDL_PushEvent(&event);
      } else if (step.action == "text") {
        const std::string& typed = step.args.empty() ? std::string() : step.args[0];
        auto copy = std::make_unique<char[]>(typed.size() + 1);
        std::memcpy(copy.get(), typed.c_str(), typed.size() + 1);
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.timestamp = SDL_GetTicksNS();
        event.text.windowID = window;
        event.text.text = copy.get();
        t.texts.push_back(std::move(copy));
        ++t.pendingTexts;
        (void)SDL_PushEvent(&event);
      } else {
        std::fprintf(stderr, "[shell-test] unknown action; step skipped\n");
      }
    }
  } // namespace

  void TestInputInit() noexcept
  {
    TestState& t = T();
    const char* const script = std::getenv("FAF_SHELL_TEST_INPUT");
    if (script == nullptr || script[0] == '\0') {
      return;
    }
    std::stringstream all(script);
    std::string item;
    while (std::getline(all, item, ';')) {
      item = Trim(item);
      if (item.empty()) {
        continue;
      }
      const std::size_t colon = item.find(':');
      if (colon == std::string::npos) {
        std::fprintf(stderr, "[shell-test] ignored step without a frame: %s\n", item.c_str());
        continue;
      }
      Step step;
      step.frame = std::strtoul(item.substr(0, colon).c_str(), nullptr, 10);
      step.text = Trim(item.substr(colon + 1));
      if (step.text.rfind("log ", 0) == 0) {
        step.action = "log";
      } else if (step.text.rfind("text ", 0) == 0) {
        step.action = "text";
        step.args.push_back(step.text.substr(5));
      } else {
        std::stringstream words(step.text);
        words >> step.action;
        std::string word;
        while (words >> word) {
          step.args.push_back(word);
        }
      }
      t.steps.push_back(step);
    }
    t.active = !t.steps.empty();
    if (t.active) {
      float gx = 0.0f;
      float gy = 0.0f;
      (void)SDL_GetGlobalMouseState(&gx, &gy);
      t.cursorX = gx;
      t.cursorY = gy;
      std::fprintf(stderr, "[shell-test] FAF_SHELL_TEST_INPUT: %zu steps; the cursor is virtual and real "
                           "mouse/keyboard input is ignored\n", t.steps.size());
    }
  }

  void TestInputFrame() noexcept
  {
    TestState& t = T();
    if (!t.active) {
      return;
    }
    ++t.frame;
    while (t.next < t.steps.size() && t.steps[t.next].frame <= t.frame) {
      Run(t.steps[t.next]);
      ++t.next;
    }
  }

  void TestInputSetShotHandler(TestShotHandler handler) noexcept
  {
    sShotHandler = handler;
  }

  bool TestInputActive() noexcept
  {
    return T().active;
  }

  bool TestInputDrops(const SDL_Event& event) noexcept
  {
    if (!T().active) {
      return false;
    }
    switch (event.type) {
      case SDL_EVENT_MOUSE_MOTION:
        return event.motion.which != kTestMouse;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.which != kTestMouse;
      case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.which != kTestMouse;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
        return event.key.which != kTestKeyboard;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Another program's window taking the focus (other test runs) must not take the
        // keyboard away from the script; the script's own "focus lost" passes.
        return event.window.data1 != kTestWindowEvent;
      case SDL_EVENT_TEXT_INPUT:
        if (T().pendingTexts > 0) {
          --T().pendingTexts;
          return false;
        }
        return true;
      default:
        return false;
    }
  }

  void GlobalMouse(float* x, float* y) noexcept
  {
    const TestState& t = T();
    if (t.active) {
      *x = t.cursorX;
      *y = t.cursorY;
      return;
    }
    (void)SDL_GetGlobalMouseState(x, y);
  }

  bool TestInputWarp(float x, float y) noexcept
  {
    TestState& t = T();
    if (!t.active) {
      return false;
    }
    t.cursorX = x;
    t.cursorY = y;
    return true;
  }
} // namespace faf_posix::shell::detail
