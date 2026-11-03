// POSIX stand-in for gpg/core/diagnostics/CrashReportDialog.cpp (the Win32 crash dialog and
// clipboard copy): the report's path goes to stderr instead, and the fatal path's hook is set the
// same way, at the original's position in the static-init order.
#include "gpg/core/diagnostics/CrashReport.h"

#include <cstdio>

namespace gpg::diag
{
  void CopyCrashReportToClipboard() {}

  void ShowCrashReportDialog(const char* const reportPath)
  {
    const char* const reportFile = (reportPath != nullptr) ? reportPath : CrashReportPath();
    std::fprintf(stderr, "FAF crashed. Crash report: %s\n", reportFile);
  }

  namespace
  {
    [[maybe_unused]] const bool gCrashReportDialogSet = (sCrashReportDialog = &ShowCrashReportDialog, true);
  } // namespace
} // namespace gpg::diag
