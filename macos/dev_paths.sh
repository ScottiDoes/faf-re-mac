# The folders the development scripts use, each one an environment variable with a default.
# Sourced by zsh once root is set to the repository.
#   FAF_RUN_DIR     the run folder the game starts in, laid out as the FAF client's bin folder:
#                   FAF's init files, with fa_path.lua and gamedata in the folder above it
#                   (default build/run/bin, which macos/make_run_folder.sh makes from the FAF
#                   client's files)
#   FAF_VULKAN_SDK  the LunarG Vulkan SDK's macOS folder (default the one macos/build_app.sh
#                   unpacks into macos/vendor)
#   FAF_CLIENT_DIR  the FAF client's data folder (default ~/.faforever); only read
FAF_RUN_DIR=${${FAF_RUN_DIR:-$root/build/run/bin}:a}
FAF_VULKAN_SDK=${FAF_VULKAN_SDK:-$root/macos/vendor/VulkanSDK/$(sed -n 's/^VULKAN_SDK_VERSION=//p' "$root/macos/vendor/versions.sh")/macOS}
FAF_CLIENT_DIR=${FAF_CLIENT_DIR:-$HOME/.faforever}

# faf_fa_install: the Forged Alliance folder named by the run folder's fa_path.lua.
faf_fa_install() {
    sed -n "s/^fa_path *= *[\"']\(.*\)[\"'].*/\1/p" "${FAF_RUN_DIR:h}/fa_path.lua" 2>/dev/null || true
}

# faf_check_run_folder <script name>: stops with a hint when FAF_RUN_DIR is not a run folder.
faf_check_run_folder() {
    [[ -f $FAF_RUN_DIR/init_faf.lua && -f ${FAF_RUN_DIR:h}/fa_path.lua && -d ${FAF_RUN_DIR:h}/gamedata ]] && return 0
    print -u2 "$1: no run folder at $FAF_RUN_DIR (macos/make_run_folder.sh makes one, or set FAF_RUN_DIR)"
    exit 2
}

# faf_note_run_folder: writes build/run_folder.lua, which the test hooks' init files
# (macos/tests/*/init_*.lua) read: InitFileDir (the run folder) and FafClientDir.
faf_note_run_folder() {
    mkdir -p "$root/build"
    print -r -- "InitFileDir = [[$FAF_RUN_DIR]]
FafClientDir = [[${FAF_CLIENT_DIR:a}]]" > "$root/build/run_folder.lua"
}
