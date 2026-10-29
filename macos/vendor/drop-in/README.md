# Drop-in folder

Files put here replace the versions pinned in `../versions.sh` the next time `macos/build_app.sh`
runs. Use it to try a newer release before pinning it. Drop-ins are not checked against a pinned
checksum, and the build says when it uses one. When there are several, the newest-named one wins.

| Put here | From | Example |
|---|---|---|
| SDL3 source release | github.com/libsdl-org/SDL/releases | `SDL3-3.4.20.tar.gz` |
| FAudio source of a release tag | github.com/FNA-XNA/FAudio/releases ("Source code (tar.gz)") | `FAudio-26.11.tar.gz` |
| LunarG Vulkan SDK for macOS, the zip | vulkan.lunarg.com/sdk/home | `vulkansdk-macos-1.4.370.0.zip` |

Keep the file names as the projects publish them: the version is read from the name. A Vulkan SDK
already installed elsewhere can be used instead with `FAF_VULKAN_SDK=<its macOS folder>`.

Once a version works, pin it in `../versions.sh` (version, URL and SHA-256) so every build gets it,
and remove the file from here.
