#!/bin/zsh
# Builds and runs the DXC bridge smoke test (no engine). Output: build/m2-shaders-tests/bridge_smoke
set -e
root=${0:A:h:h:h:h}
sdk=${FAF_VULKAN_SDK:-$HOME/Gaming/VulkanSDK/1.4.363.0/macOS}
out=$root/build/m2-shaders-tests
mkdir -p $out
clang++ -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
  -DFAF_VULKAN_SDK_DIR="\"$sdk\"" -I$root/src/platform/posix/dxc -I$sdk/include/dxc \
  $root/src/platform/posix/dxc/DxcBridge.cpp $root/macos/tests/shaders/bridge_smoke.cpp -o $out/bridge_smoke
$out/bridge_smoke $out/smoke.spv
$sdk/bin/spirv-val --target-env vulkan1.1 $out/smoke.spv && echo "spirv-val: ok"
