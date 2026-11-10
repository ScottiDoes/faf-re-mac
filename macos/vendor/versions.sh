# The third-party pieces macos/build_app.sh puts into FAF Native.app, pinned to versions tested
# together, with the SHA-256 each download must have. To move to a newer one, either drop its
# file into macos/vendor/drop-in (the build uses it instead, unchecked, and says so) or change it
# here: version, URL and checksum (from the project's release page, or `shasum -a 256 <file>`).
#
# Sourced by zsh; plain NAME=value lines only.

# The macOS version the app is built for (the game, SDL3, FAudio and the setup window).
MACOS_DEPLOYMENT_TARGET=14.0

# SDL3: window, input, audio output. Source release, built here.
SDL3_VERSION=3.4.18
SDL3_URL=https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
SDL3_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3

# FAudio: XACT sound (FACT), on SDL3. Source of the release tag, built here.
FAUDIO_VERSION=26.10
FAUDIO_URL=https://github.com/FNA-XNA/FAudio/archive/refs/tags/26.10.tar.gz
FAUDIO_FILE=FAudio-26.10.tar.gz
FAUDIO_SHA256=c508f297ec8d065b456b2d9d74bab89ffc84ac3afbcb428bd54e40f09f8b901e

# LunarG Vulkan SDK: the Vulkan loader, MoltenVK and DXC (Microsoft publishes no macOS DXC).
# Unpacked into macos/vendor/VulkanSDK with its own installer; nothing is installed system-wide.
VULKAN_SDK_VERSION=1.4.363.0
VULKAN_SDK_URL=https://sdk.lunarg.com/sdk/download/1.4.363.0/mac/vulkansdk-macos-1.4.363.0.zip
VULKAN_SDK_SHA256=163431e0203c8215286a17cf393643ce6b951f77d1b4f74ab30821a1f0beb09b

# Build tools, fetched only when this Mac has none (or too old a CMake).
CMAKE_MIN_VERSION=3.25
CMAKE_VERSION=4.4.5
CMAKE_URL=https://github.com/Kitware/CMake/releases/download/v4.4.5/cmake-4.4.5-macos-universal.tar.gz
CMAKE_SHA256=679e77a96b9ef9484cc6c1445255fc4f84206b48a65420efbedf16bdab55fbff
NINJA_VERSION=1.13.2
NINJA_URL=https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip
NINJA_SHA256=c99048673aa765960a99cf10c6ddb9f1fad506099ff0a0e137ad8960a88f321b

# Boost 1.34.1 (engine source dependency; the same pin as cmake/FafDependencies.cmake).
BOOST_URL=https://downloads.sourceforge.net/project/boost/boost/1.34.1/boost_1_34_1.tar.bz2
BOOST_SHA256=0f866c75b025a4f1340117a106595cc0675f48ba1e5a9b5c221ec7f19e96ec4c

# Zstandard and Monocypher (returning players' saves and rejoin keys; the net channel): the same
# pins as cmake/FafDependencies.cmake. Source archives, compiled by macos/deps.cmake.
ZSTD_URL=https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
MONOCYPHER_URL=https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.2.tar.gz
MONOCYPHER_FILE=Monocypher-4.0.2.tar.gz
MONOCYPHER_SHA256=bc1ca30b1b2654e4e7daf2492c0d204200e55137f23fda6b7142fd7d523bd6b4

# FreeType and msdfgen (sharp MSDF text): the same pins as cmake/FafDependencies.cmake. Source
# archives, compiled through cmake/deps/freetype and cmake/deps/msdfgen (macos/deps.cmake).
FREETYPE_URL=https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.gz
FREETYPE_SHA256=9186627467a616d9a9e3e574748ac0c45fa77398e6364edef3041a8ba8d92f64
MSDFGEN_URL=https://github.com/Chlumsky/msdfgen/archive/refs/tags/v1.13.tar.gz
MSDFGEN_FILE=msdfgen-1.13.tar.gz
MSDFGEN_SHA256=93cd1ad8918c1a78c5c96e82d4f4c77f0eb86c2e7e8579a0967e54196c4b7167
