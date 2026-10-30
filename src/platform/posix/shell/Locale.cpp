// The Mac's preferred languages (Shell.h, PreferredLanguage), from which the game picks its
// language when the player has not chosen one (CScApp.cpp).

#include "platform/posix/shell/Shell.h"

#include <CoreFoundation/CoreFoundation.h>

namespace faf_posix::shell
{
  bool PreferredLanguage(const int index, char* const tag, const int size) noexcept
  {
    if (index < 0 || tag == nullptr || size <= 0) {
      return false;
    }
    CFArrayRef languages = CFLocaleCopyPreferredLanguages();
    if (languages == nullptr) {
      return false;
    }
    bool found = false;
    if (index < CFArrayGetCount(languages)) {
      const auto language = static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, index));
      found = language != nullptr && CFStringGetCString(language, tag, size, kCFStringEncodingUTF8);
    }
    CFRelease(languages);
    return found;
  }
} // namespace faf_posix::shell
