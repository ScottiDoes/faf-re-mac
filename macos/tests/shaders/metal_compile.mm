// Compiles MSL files with the Metal runtime compiler (MTLDevice newLibraryWithSource), as
// MoltenVK does at pipeline creation; no window, no offline Metal toolchain needed.
// Usage: metal_compile <file.metal>...   prints one FAIL line per file that does not compile.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>

int main(int argc, char** argv)
{
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
      std::fprintf(stderr, "no Metal device\n");
      return 2;
    }
    MTLCompileOptions* options = [MTLCompileOptions new];
    options.languageVersion = MTLLanguageVersion3_0;
    int failures = 0;
    int compiled = 0;
    for (int index = 1; index < argc; ++index) {
      @autoreleasepool {
        NSError* error = nil;
        NSString* source = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:argv[index]] encoding:NSUTF8StringEncoding error:&error];
        if (source == nil) {
          std::printf("FAIL %s: unreadable\n", argv[index]);
          ++failures;
          continue;
        }
        id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
        if (library == nil) {
          std::printf("FAIL %s: %s\n", argv[index], error.localizedDescription.UTF8String);
          ++failures;
        } else {
          ++compiled;
        }
      }
    }
    std::printf("METAL %s compiled=%d failed=%d\n", device.name.UTF8String, compiled, failures);
    return failures != 0 ? 1 : 0;
  }
}
