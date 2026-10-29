# Building FAF Native for macOS

FAF Native is this engine built natively for Macs with Apple silicon, packaged as one app:

- **a setup window**, opened by double-clicking the app. It finds (or copies in) your Forged Alliance files, downloads and sets up the FAF client, and connects the two.
- **the game**, `Contents/Helpers/FAF Native Game.app`, with every library it needs inside it.
- **the FAF client's launcher**, `Contents/Resources/faf-launch`. The client starts games through it in place of the Windows game.

## One command

On a Mac with Apple silicon and macOS 14 or later, with Xcode or Apple's Command Line Tools (`xcode-select --install`):

```sh
git clone -b macos-native https://github.com/ScottiDoes/faf-re-mac.git faf-re
faf-re/macos/build_app.sh
```

The app lands in `build/dist`: `FAF Native.app`, and the same as `FAF-Native-<commit>.zip` to pass on. The first run downloads about 520 MB and compiles for a while. After that, running it again only redoes what changed.

### What the script does

1. **Checks this Mac:** Apple silicon, the macOS version, the compilers, and a git clone.
2. **Build tools:** uses CMake (3.25+) and Ninja if this Mac has them, otherwise fetches them.
3. **Engine source dependencies:**
   - the git submodules: Wild Magic 3.8, zlib 1.2.3, the Vulkan headers and FFmpeg;
   - Boost 1.34.1;
   - then the FAF patches from `dependencies/patches` (the `_required` ones and the `_macos` ones).
4. **SDL3 and FAudio:** builds them from their source releases, for macOS 14.
5. **The LunarG Vulkan SDK:** unpacks it with its own installer into `macos/vendor/VulkanSDK`, after asking you to accept its licences. It supplies the Vulkan loader, MoltenVK and DXC. Nothing is installed system-wide.
6. **The game:** builds it into `build/release`.
7. **The app:** packages it with `macos/app/make_app.sh`.

Downloads are kept in `macos/vendor/downloads`, and each is checked against the SHA-256 pinned in `macos/vendor/versions.sh`:

| | Version | Size | From |
|---|---|---|---|
| LunarG Vulkan SDK | 1.4.363.0 | 396 MB | sdk.lunarg.com |
| SDL3 (source) | 3.4.18 | 16 MB | github.com/libsdl-org/SDL |
| FAudio (source) | 26.10 | 1 MB | github.com/FNA-XNA/FAudio |
| Boost (source) | 1.34.1 | 13 MB | SourceForge |
| CMake, Ninja (only if missing) | 4.4.5, 1.13.2 | 90 MB | github.com/Kitware/CMake, github.com/ninja-build/ninja |
| Submodules | pinned commits | | GitHub |

Everything under `macos/vendor` except the pins and the drop-in instructions is ignored by git. About 4 GB of space is needed: the Vulkan SDK takes 2.5 GB, the builds the rest.

Options (environment variables):

| Variable | Effect |
|---|---|
| `ACCEPT_VULKAN_SDK_LICENSE=1` | accept the Vulkan SDK's licences without being asked (unattended builds) |
| `FAF_VULKAN_SDK=<dir>` | use a Vulkan SDK installed elsewhere (its `macOS` folder) |
| `OUT=<dir>` | where the app and zip go |
| `SIGN_IDENTITY=<identity>` | sign with this identity instead of ad hoc (see below) |
| `BUNDLE_ID=<id>` | the app's bundle identifier; the game's is `<id>.game` (default `local.faf-re-mac`) |
| `JOBS=<n>` | parallel compile jobs |

## Newer versions

`macos/vendor/versions.sh` pins SDL3, FAudio and the Vulkan SDK to versions tested together. To try a newer one, drop its file into `macos/vendor/drop-in` (see the README there) and run the script again. It uses the drop-in instead and says so. When the new version works, pin it in `versions.sh` and remove the drop-in.

The FAF client is not built into the app. The setup window downloads the newest one when the player asks:
- FAF's Unix build, from GitHub;
- a Java runtime for Intel Macs, from Adoptium (it runs under Rosetta);
- the macOS JavaFX libraries, from Maven Central.

Each is checked against its source's checksum. The window offers an update when FAF publishes a new client.

## Using the app

Copy `FAF Native.app` into Applications and open it. The window walks through the rest:

1. **Forged Alliance files.** The player's own copy (Steam, GOG or disc) is used where it is, for example in a CrossOver or Wine bottle, or in a folder copied from a Windows PC. It can also be copied into the app's own folder.
2. **The FAF client.** The window downloads it, installs Rosetta if needed (macOS asks for an administrator password), and starts it. After the first login, **Apply for Me** points the client at FAF Native and at the Forged Alliance files, keeping a backup of the client's settings.
3. **Playing.** Start games from the FAF client as usual.

A player's Mac needs Apple silicon, macOS 14 or later, and Rosetta for the FAF client's Java runtime. The app keeps its data in `~/Library/Application Support/FAF Native` and its logs in `~/Library/Logs/FAF Native`.

## Sharing the app

An app signed ad hoc (the default) runs on the Mac that built it. On another Mac, macOS blocks its first launch, because the app comes from an unidentified developer. Either:
- open it once from System Settings → Privacy & Security → **Open Anyway**; or
- clear the download flag with `xattr -dr com.apple.quarantine "/Applications/FAF Native.app"`.

Use the `xattr` command if games will be started from the FAF client before the app has been opened.

For an app that opens anywhere without this, sign it with a Developer ID Application certificate and have Apple notarize it:

```sh
SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" macos/build_app.sh
xcrun notarytool submit "build/dist/FAF-Native-<commit>.zip" --keychain-profile <profile> --wait
xcrun stapler staple "build/dist/FAF Native.app"
```

These notarization steps are not automated or tested yet. `make_app.sh` already signs with the hardened runtime and a secure timestamp when given an identity.

## For development

- `macos/app/deploy.sh` builds the development tree (`build/macos`, Homebrew's SDL3 and FAudio), checks it against a recorded replay, and installs the app on this Mac, keeping the previous one for `deploy.sh rollback`.
- `macos/tests/app/snapshot_setup.sh` draws the setup window offscreen into PNGs for a few situations.
- `macos/run_game.sh` and `macos/run_replay.sh` run the game and the headless replay checker.
