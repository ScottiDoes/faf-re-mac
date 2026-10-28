# Package "wx" (wxWidgets stand-in, app shell, UI): translation units left out of the
# macOS build (each replaced in place by its stand-in). wxWidgets itself is replaced by the header-only stand-in in
# src/platform/posix/wx (see its wx/defs.h); everything else of the package compiles.

list(APPEND FAF_MACOS_REPLACED_SOURCES
  # The Windows application shell: low-level keyboard hook, SEH filter and crash dialog,
  # DbgHelp, registry, clipboard. Replaced by src/platform/posix/stubs/wx/WinAppPosix.cpp,
  # which defines everything WinApp.h exports (the application loop for real).
  "^moho/app/WinApp\\.cpp$=>platform/posix/stubs/wx/WinAppPosix.cpp"
  # WinMain and the Windows-only startup around it (accessibility keys, CRT diagnostics,
  # the allocation log, /crashtest). The headless runner has its own main(); its console
  # commands are registered by src/platform/posix/stubs/wx/WinMainPosix.cpp.
  "^moho/app/WinMain\\.cpp$=>platform/posix/stubs/wx/WinMainPosix.cpp"
)
