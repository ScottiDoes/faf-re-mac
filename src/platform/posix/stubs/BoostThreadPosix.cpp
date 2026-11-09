// POSIX stand-in for boost's libs/thread/src/thread.cpp (replaced in macos/exclusions.cmake): the
// same file, with every thread given a Windows-sized stack. macOS gives a new pthread 512 KB, and
// the sim thread's save recursion (Sim::SaveState) overflows that; Windows threads get the
// executable's 2 to 4 MB reserve. 8 MB, as arm64 frames are larger; it's only address space until used.

#include <pthread.h>

namespace
{
  int CreateThreadWithFullStack(pthread_t* const thread, const pthread_attr_t*, void* (*const start)(void*), void* const argument)
  {
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 8u * 1024u * 1024u);
    const int result = pthread_create(thread, &attributes, start, argument);
    pthread_attr_destroy(&attributes);
    return result;
  }
} // namespace

#define pthread_create CreateThreadWithFullStack
#include "libs/thread/src/thread.cpp"
#undef pthread_create
