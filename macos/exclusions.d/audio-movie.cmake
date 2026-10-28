# Package audio (sound since milestone 3; the movie entries moved to movies.cmake):
# translation units of src/sources.cmake replaced in the macOS build.
# Each entry says why and which file under src/platform/posix/ defines its symbols instead.
# Notes: macos/porting/audio.md (and audio-movie.md for wave 1).

# PE import-table patch of xactengine2_9.dll (VirtualProtect, GetModuleHandleExW,
# IMAGE_* walking): no XACT DLL exists on POSIX, and FACT's clock already counts from
# process start (what the patch gives XACT).
# Replaced by audio/XactClockPosix.cpp (SND_InstallXactClock: nothing to patch).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^moho/audio/XactClock\\.cpp$=>platform/posix/audio/XactClockPosix.cpp")
