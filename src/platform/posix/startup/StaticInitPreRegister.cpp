// The POSIX half of gpg/core/reflection/StaticInitPhase.h: runs every
// GPG_PREREGISTER_INIT provider before any other dynamic initializer of the engine,
// as the CRT's walk of `.CRT$XCL` does on Windows.
//
// macos/CMakeLists.txt links this object first; Mach-O runs an image's initializers
// in link order, so this one goes before the engine's ordinary initializers. It also
// publishes the command line first, so engine initializers see __argc/__argv as they
// do on Windows (where the CRT sets them before any initializer).

extern "C" void faf_posixInitArguments(int argc, char** argv) noexcept;

using PreRegisterFn = void (*)();

extern PreRegisterFn const gFafPreRegisterBegin[] __asm("section$start$__DATA$__faf_prereg");
extern PreRegisterFn const gFafPreRegisterEnd[] __asm("section$end$__DATA$__faf_prereg");

namespace
{
  // The only initializer in this object, so nothing else here can run before it.
  // Darwin passes (argc, argv, envp) to image initializers.
  __attribute__((constructor)) void RunFirstInitializers(int argc, char** argv, char**)
  {
    faf_posixInitArguments(argc, argv);
    for (const PreRegisterFn* fn = gFafPreRegisterBegin; fn != gFafPreRegisterEnd; ++fn) {
      (*fn)();
    }
  }
} // namespace
