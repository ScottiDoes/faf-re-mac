// Runs faf_posixRunSelfTests as the headless runner does, against the x87 test double
// (x87_test_double.cpp). `selftest_main --expect-fail` inverts the exit status: built with
// the deliberately wrong test double, every x87 known answer must be reported as failing.
#include "platform/posix/crt/SelfTest.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
  const bool expectFail = argc > 1 && std::strcmp(argv[1], "--expect-fail") == 0;
  const bool passed = faf_posixRunSelfTests(true);
  std::printf("faf_posixRunSelfTests: %s\n", passed ? "passed" : "FAILED");
  return passed != expectFail ? 0 : 1;
}
