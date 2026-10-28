// The NSView behind a window's CAMetalLayer, placed with the Objective-C runtime.
//
// SDL_Metal_CreateView adds a layer-backed view (its layer a CAMetalLayer) to the window's
// content view, sized to it and following its size; SDL keeps the layer's drawableSize and
// contentsScale in step with the view (its updateDrawableSize). A child HWND's view is moved
// over the child's rectangle instead. The runtime is called directly because the Win32
// stand-ins (BOOL) and <objc/objc.h> cannot share a translation unit.

#include "platform/posix/shell/ShellState.h"

extern "C" {
// <objc/runtime.h>, <objc/message.h> (OBJC_OLD_DISPATCH_PROTOTYPES=0 spelling).
void* sel_registerName(const char* name);
void objc_msgSend(void);
}

namespace faf_posix::shell::detail
{
  namespace
  {
    // CGRect: four CGFloats (double on 64-bit), an HFA passed and returned in d0-d3.
    struct Rect
    {
      double x;
      double y;
      double width;
      double height;
    };

    template <typename Result, typename... Args>
    Result Message(void* receiver, const char* selector, Args... args) noexcept
    {
      using Function = Result (*)(void*, void*, Args...);
      return reinterpret_cast<Function>(&objc_msgSend)(receiver, sel_registerName(selector), args...);
    }

    constexpr unsigned long kViewWidthSizable = 2;   // NSViewWidthSizable
    constexpr unsigned long kViewHeightSizable = 16; // NSViewHeightSizable
  } // namespace

  void SetMetalViewFrame(SDL_MetalView view, double x, double y, double width, double height, bool fill) noexcept
  {
    if (view == nullptr) {
      return;
    }
    void* const superview = Message<void*>(view, "superview");
    if (superview == nullptr) {
      return;
    }
    const Rect bounds = Message<Rect>(superview, "bounds");
    if (fill) {
      Message<void>(view, "setAutoresizingMask:", kViewWidthSizable | kViewHeightSizable);
      Message<void>(view, "setFrame:", bounds);
    } else {
      const bool flipped = Message<bool>(superview, "isFlipped");
      const Rect frame{x, flipped ? y : bounds.height - y - height, width, height};
      Message<void>(view, "setAutoresizingMask:", 0ul);
      Message<void>(view, "setFrame:", frame);
    }
    void* const update = sel_registerName("updateDrawableSize");
    if (Message<bool>(view, "respondsToSelector:", update)) {
      Message<void>(view, "updateDrawableSize");
    }
  }

  void SetMetalViewHidden(SDL_MetalView view, bool hidden) noexcept
  {
    if (view == nullptr) {
      return;
    }
    Message<void>(view, "setHidden:", hidden);
  }
} // namespace faf_posix::shell::detail
