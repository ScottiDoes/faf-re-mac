// SoftFloat 3e platform header for Apple clang on arm64 (the build/<platform>/platform.h
// SoftFloat expects). Used only by the faf_softfloat test oracle, see ../deps.cmake.
#define LITTLEENDIAN 1
#define INLINE static inline
#define SOFTFLOAT_BUILTIN_CLZ 1
#define SOFTFLOAT_INTRINSIC_INT128 1
#include "opts-GCC.h"
