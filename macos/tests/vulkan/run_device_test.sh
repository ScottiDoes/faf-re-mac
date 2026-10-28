#!/bin/zsh
# Runs vk_device_test (build it first: see CMakeLists.txt) under a watchdog.
#
#   macos/tests/vulkan/run_device_test.sh <moltenvk|kosmickrisp> <plain|validate|screenshot> [test args...]
#
# validate:   FAF_VULKAN_VALIDATION=1 (VK_LAYER_KHRONOS_validation through the engine's
#             debug messenger); prints the number of distinct validation messages.
# screenshot: VK_LAYER_LUNARG_screenshot writes frames to PPM, converted to PNG.
# Output in build/m2-vulkan-tests/runs/<driver>-<mode>/ (log.txt, *.png).
set -u
here=${0:A:h}
root=${here:h:h:h}
build=$root/build/m2-vulkan-tests
driver=${1:-moltenvk}
mode=${2:-plain}
shift 2 2>/dev/null || shift $#
out=$build/runs/$driver-$mode
mkdir -p $out
rm -f $out/*.ppm(N) $out/*.png(N) $out/log.txt(N)

export FAF_VULKAN_DRIVER=$driver
case $mode in
  validate)
    export FAF_VULKAN_VALIDATION=1
    ;;
  screenshot)
    export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot
    export VK_SCREENSHOT_FRAMES=${VK_SCREENSHOT_FRAMES:-5,40,59,61,90,119}
    export VK_SCREENSHOT_DIR=$out
    ;;
esac

python3 -I $here/watchdog.py ${WATCHDOG_SECONDS:-60} $build/vk_device_test "$@" > $out/log.txt 2>&1
rc=$?
grep -E "^(test|warning)|Vulkan: (ready|loader|portability|texture set|swap chain|head|device created|acquiring|present)" $out/log.txt
if [[ $mode == validate ]]; then
  print "validation messages: $(grep -c 'Vulkan validation:' $out/log.txt)"
fi
if [[ $mode == screenshot ]]; then
  ppms=($out/*.ppm(N))
  (( ${#ppms} )) && python3 -I $here/ppm2png.py $ppms
fi
print "exit $rc ($out)"
exit $rc
