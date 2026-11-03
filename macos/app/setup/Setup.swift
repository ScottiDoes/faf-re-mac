// FAF Native's setup model (the window the app opens when double-clicked): where the Forged
// Alliance files and the FAF client's data are, whether the client starts games through this
// app, and copying the game files into the app's own folder. The game itself is the nested app
// Contents/Helpers/FAF Native Game.app; the FAF client starts it through
// Contents/Resources/faf-launch (macos/app/faf-launch), which needs none of this: the client
// tells the game where Forged Alliance is (its fa_path.lua). The setup only finds, checks and
// explains. It never writes into the client's folder (~/.faforever).
import AppKit
import Foundation

/// Folders the app owns or reads.
enum Places {
    /// The user's home; FAF_NATIVE_TEST_HOME stands in for it in macos/tests/app/snapshot_setup.sh.
    static let home = ProcessInfo.processInfo.environment["FAF_NATIVE_TEST_HOME"].map { URL(fileURLWithPath: $0, isDirectory: true) }
        ?? FileManager.default.homeDirectoryForCurrentUser
    static let appSupport = home.appendingPathComponent("Library/Application Support/FAF Native", isDirectory: true)
    static let config = appSupport.appendingPathComponent("config.json")
    static let copiedGame = appSupport.appendingPathComponent("Forged Alliance", isDirectory: true)
    static let logs = home.appendingPathComponent("Library/Logs/FAF Native", isDirectory: true)
    static let clientData = home.appendingPathComponent(".faforever", isDirectory: true)
    static var launcher: URL { Bundle.main.bundleURL.appendingPathComponent("Contents/Resources/faf-launch") }
    static var gameApp: URL { Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/FAF Native Game.app") }
    /// The line for the client's executable decorator setting.
    static var launcherLine: String { "\"\(launcher.path)\" \"%s\"" }
    /// macOS runs an app that still carries the download flag from a hidden temporary copy ("App
    /// Translocation", .../AppTranslocation/...). That path changes, so the client can't keep it as
    /// its launcher. (FAF_NATIVE_TEST_TRANSLOCATED=1 stands for it in a test home.)
    static var translocated: Bool {
        if ProcessInfo.processInfo.environment["FAF_NATIVE_TEST_HOME"] != nil {
            return ProcessInfo.processInfo.environment["FAF_NATIVE_TEST_TRANSLOCATED"] == "1"
        }
        return Bundle.main.bundlePath.contains("/AppTranslocation/")
    }
    /// The command that clears the download flag once the app is in Applications.
    static let clearFlagCommand = "xattr -dr com.apple.quarantine \"/Applications/FAF Native.app\""
}

/// What the setup remembers between runs (config.json in Places.appSupport).
struct Config: Codable {
    var gamePath: String?
    /// The full setup has been on screen with everything ready: later launches open the simple window.
    var setupSeen: Bool?

    static func load() -> Config {
        guard let data = try? Data(contentsOf: Places.config),
              let config = try? JSONDecoder().decode(Config.self, from: data) else { return Config() }
        return config
    }

    func save() throws {
        try FileManager.default.createDirectory(at: Places.appSupport, withIntermediateDirectories: true)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(self).write(to: Places.config, options: .atomic)
    }
}

/// A Forged Alliance installation folder, checked for what FAF's init.lua mounts (gamedata,
/// sounds, movies) and what the client copies from it (bin).
struct GameFolder: Identifiable, Equatable {
    let url: URL
    let missing: [String]
    var id: String { url.path }
    var isValid: Bool { missing.isEmpty }
    var isAppCopy: Bool { url.standardizedFileURL.path == Places.copiedGame.standardizedFileURL.path }

    /// Desktop, Documents, Downloads or iCloud Drive: folders macOS guards with a privacy permission,
    /// where iCloud's Desktop & Documents sync can also leave only placeholders of big files.
    var guardedFolder: String? {
        let path = url.standardizedFileURL.path
        for name in ["Desktop", "Documents", "Downloads"] where path.hasPrefix(Places.home.appendingPathComponent(name).path + "/") {
            return name
        }
        return path.contains("/Library/Mobile Documents/") ? "iCloud Drive" : nil
    }

    static let required = [
        "bin/SupremeCommander.exe", "gamedata/units.scd", "gamedata/textures.scd", "gamedata/meshes.scd",
        "gamedata/env.scd", "gamedata/effects.scd", "sounds", "movies",
    ]
    /// The folders a copy takes (Steam's DirectX installer and install script stay behind).
    static let copiedFolders = ["bin", "gamedata", "sounds", "movies", "maps", "fonts"]

    init(_ url: URL) {
        self.url = url
        let fm = FileManager.default
        missing = Self.required.filter { !fm.fileExists(atPath: url.appendingPathComponent($0).path) }
    }
}

/// Where Forged Alliance usually is on a Mac: inside the Wine prefix of a Wine front end
/// (CrossOver, wrapper apps such as Sikarugir, Kegworks or Wineskin, plain Wine), installed by
/// Steam, GOG or the retail disc. Folders other apps keep in ~/Library/Containers are not
/// searched (macOS would ask for permission each time); "Choose Folder…" covers those.
enum GameSearch {
    static let installFolders = [
        "Program Files (x86)/Steam/steamapps/common/Supreme Commander Forged Alliance",
        "Program Files/Steam/steamapps/common/Supreme Commander Forged Alliance",
        "GOG Games/Supreme Commander - Forged Alliance",
        "Program Files (x86)/GOG Galaxy/Games/Supreme Commander - Forged Alliance",
        "Program Files (x86)/THQ/Gas Powered Games/Supreme Commander - Forged Alliance",
    ]

    /// The drive_c folders of the Wine prefixes found.
    static func wineDrives() -> [URL] {
        let fm = FileManager.default
        func children(_ url: URL) -> [URL] {
            (try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil, options: [.skipsHiddenFiles])) ?? []
        }
        var drives: [URL] = []
        for bottle in children(Places.home.appendingPathComponent("Library/Application Support/CrossOver/Bottles")) {
            drives.append(bottle.appendingPathComponent("drive_c"))
        }
        let appFolders = ["Applications", "Applications/Sikarugir", "Applications/Kegworks", "Applications/Wineskin"]
            .map { Places.home.appendingPathComponent($0) } + [URL(fileURLWithPath: "/Applications")]
        for folder in appFolders {
            for app in children(folder) where app.pathExtension == "app" {
                drives.append(app.appendingPathComponent("Contents/SharedSupport/prefix/drive_c"))
            }
        }
        drives.append(Places.home.appendingPathComponent(".wine/drive_c"))
        return drives.filter { fm.fileExists(atPath: $0.path) }
    }

    /// Valid installations, the FAF client's own choice and this app's copy first.
    static func installations(client: ClientInfo) -> [GameFolder] {
        var urls: [URL] = []
        if let path = client.installationPath { urls.append(URL(fileURLWithPath: path)) }
        urls.append(Places.copiedGame)
        for drive in wineDrives() {
            urls += installFolders.map { drive.appendingPathComponent($0) }
        }
        var seen = Set<String>()
        return urls.filter { seen.insert($0.standardizedFileURL.path).inserted }.map(GameFolder.init).filter(\.isValid)
    }
}

/// How the FAF client starts the game: its executable decorator setting.
enum Hookup: Equatable {
    case noClient
    case thisApp
    /// Another launcher script that hands over to FAF Native (it mentions faf-launch).
    case handsOver(String)
    case otherLauncher(String)
    case none
}

/// What the FAF client has: its data folder (~/.faforever) and settings (client.prefs).
struct ClientInfo: Equatable {
    var hasData = false
    var hasPrefs = false        // client.prefs exists: the client has run (and been logged in to) here
    var prefsUnreadable = false // client.prefs isn't a JSON object (the client is half way through writing it)
    var gameType: String?
    var gameVersion: String?
    var installationPath: String?
    var decorator: String?

    static func read() -> ClientInfo {
        var info = ClientInfo()
        let fm = FileManager.default
        info.hasData = fm.fileExists(atPath: Places.clientData.appendingPathComponent("bin/init.lua").path)
            && fm.fileExists(atPath: Places.clientData.appendingPathComponent("gamedata").path)
        if let text = try? String(contentsOf: Places.clientData.appendingPathComponent("fa_path.lua"), encoding: .utf8) {
            info.gameType = luaString(text, "GameType")
            info.gameVersion = luaString(text, "GameVersion")
        }
        info.hasPrefs = fm.fileExists(atPath: Places.clientData.appendingPathComponent("client.prefs").path)
        if let data = try? Data(contentsOf: Places.clientData.appendingPathComponent("client.prefs")) {
            if let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                let fa = root["forgedAlliance"] as? [String: Any]
                info.installationPath = fa?["installationPath"] as? String
                info.decorator = fa?["executableDecorator"] as? String
            } else {
                info.prefsUnreadable = true
            }
        }
        return info
    }

    /// `Name = "value"` in fa_path.lua.
    private static func luaString(_ text: String, _ name: String) -> String? {
        for line in text.split(separator: "\n") where line.hasPrefix(name + " ") || line.hasPrefix(name + "=") {
            let parts = line.split(separator: "\"")
            if parts.count >= 2 { return String(parts[1]) }
        }
        return nil
    }

    var hookup: Hookup {
        // Settings without a decorator (a client that has run but never had one set) count as .none.
        guard hasData || hasPrefs || decorator != nil else { return .noClient }
        let decorator = (self.decorator ?? "").trimmingCharacters(in: .whitespaces)
        if decorator.isEmpty || decorator == "%s" || decorator == "\"%s\"" { return .none }
        if decorator.contains(Places.launcher.path) { return .thisApp }
        // The program is the first word, or the first quoted string.
        let program: String
        if decorator.hasPrefix("\"") {
            program = String(decorator.dropFirst().prefix { $0 != "\"" })
        } else {
            program = String(decorator.prefix { $0 != " " })
        }
        if let script = try? String(contentsOfFile: program, encoding: .utf8), script.contains("faf-launch") {
            return .handsOver(program)
        }
        return .otherLauncher(program)
    }
}

/// Copies the folders a game needs into Places.copiedGame, through a .partial folder that is
/// renamed at the end, so a cancelled or failed copy leaves nothing half-done behind.
enum GameCopy {
    struct Failure: LocalizedError {
        let errorDescription: String?
    }

    static func size(of folder: URL) -> Int64 {
        files(in: folder).reduce(0) { $0 + $1.size }
    }

    /// (path relative to `folder`, size) of every file the copy takes.
    static func files(in folder: URL) -> [(relative: String, size: Int64)] {
        var result: [(String, Int64)] = []
        let fm = FileManager.default
        for top in GameFolder.copiedFolders {
            let base = folder.appendingPathComponent(top)
            guard let walker = fm.enumerator(at: base, includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey]) else { continue }
            for case let url as URL in walker {
                guard let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]),
                      values.isRegularFile == true else { continue }
                let relative = top + "/" + url.path.dropFirst(base.path.count + 1)
                result.append((relative, Int64(values.fileSize ?? 0)))
            }
        }
        return result
    }

    /// Runs off the main thread. `progress(copied, total)` after each file; `cancelled()` is
    /// polled between files.
    static func run(from source: URL, progress: @escaping (Int64, Int64) -> Void,
                    cancelled: @escaping () -> Bool) throws {
        let fm = FileManager.default
        let list = files(in: source)
        let total = list.reduce(0) { $0 + $1.size }
        try fm.createDirectory(at: Places.appSupport, withIntermediateDirectories: true)
        let free = (try? Places.appSupport.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey]))?
            .volumeAvailableCapacityForImportantUsage ?? Int64.max
        if free < total + 512 * 1024 * 1024 {
            throw Failure(errorDescription: String(format: "Not enough free space: the copy needs %.1f GB, the disk has %.1f GB free.",
                                                   Double(total) / 1e9, Double(free) / 1e9))
        }
        let partial = Places.appSupport.appendingPathComponent("Forged Alliance.partial", isDirectory: true)
        try? fm.removeItem(at: partial)
        var copied: Int64 = 0
        do {
            for file in list {
                if cancelled() { throw CancellationError() }
                let target = partial.appendingPathComponent(file.relative)
                try fm.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
                try fm.copyItem(at: source.appendingPathComponent(file.relative), to: target)
                copied += file.size
                progress(copied, total)
            }
            guard GameFolder(partial).isValid else {
                throw Failure(errorDescription: "The copy is missing files the game needs.")
            }
            if fm.fileExists(atPath: Places.copiedGame.path) { try fm.removeItem(at: Places.copiedGame) }
            try fm.moveItem(at: partial, to: Places.copiedGame)
        } catch {
            try? fm.removeItem(at: partial)
            throw error
        }
    }
}

/// A flag the copy's background thread polls.
final class CancelFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var raised = false
    var isRaised: Bool { lock.withLock { raised } }
    func raise() { lock.withLock { raised = true } }
}

/// The window's state.
@MainActor
final class SetupModel: ObservableObject {
    @Published private(set) var client = ClientInfo()
    @Published private(set) var game: GameFolder?
    @Published private(set) var others: [GameFolder] = []
    @Published private(set) var gameSize: Int64?
    @Published private(set) var copyFraction: Double?
    @Published private(set) var copyText = ""
    @Published var alert: String?
    // The FAF client set up by FAF Native (ClientSetup.swift).
    @Published private(set) var rosettaInstalled = true
    @Published private(set) var installedClient: InstalledClient?
    @Published private(set) var latestRelease: ClientRelease?
    @Published private(set) var releaseProblem: String?
    @Published private(set) var checkingClientUpdate = false
    @Published var pendingClientUpdate: ClientRelease?
    @Published private(set) var clientRunning = false
    @Published private(set) var clientLaunching = false
    @Published private(set) var clientPrefsExist = false
    @Published private(set) var installFraction: Double?
    @Published private(set) var installText = ""
    @Published private(set) var rosettaInstalling = false
    /// What FAF Native is doing to the client right now (stopping it, resetting its window).
    @Published private(set) var clientBusy: String?
    /// The simple window (SimpleView.swift) instead of the full setup: decided at launch, and left
    /// for the full setup when the player asks or something stops being ready.
    @Published private(set) var simpleMode = false
    private var lastAutoApplyAttempt: String?
    private var installTask: Task<Void, Never>?
    private var config = Config.load()
    private var cancelFlag = CancelFlag()

    init() {
        refresh()
        simpleMode = config.setupSeen == true && simpleModePossible
    }

    func refresh() {
        rosettaInstalled = FileManager.default.fileExists(atPath: Places.rosetta.path)
        installedClient = InstalledClient.load()
        clientRunning = ClientRunner.isRunning()
        clientPrefsExist = FileManager.default.fileExists(atPath: Places.clientPrefs.path)
        checkLatestRelease()
        let info = ClientInfo.read()
        if !info.prefsUnreadable { client = info }
        let found = GameSearch.installations(client: client)
        if let saved = config.gamePath {
            game = GameFolder(URL(fileURLWithPath: saved))
        } else {
            game = found.first
        }
        others = found.filter { $0 != game }
        measureGame()
        autoApplyClientSettingsIfReady()
        if simpleMode && !simpleModePossible { simpleMode = false }
    }

    // MARK: Readiness

    var gameReady: Bool { game?.isValid == true }

    /// Games started in the client open in FAF Native (this copy, or a launcher that hands over to it).
    var clientConnected: Bool {
        switch client.hookup {
        case .thisApp, .handsOver: return true
        case .noClient, .none, .otherLauncher: return false
        }
    }

    /// The client's Forged Alliance location differs from the folder chosen in step 1.
    var clientLocationDiffers: Bool {
        guard let game, game.isValid, let clientPath = client.installationPath else { return false }
        return URL(fileURLWithPath: clientPath).standardizedFileURL.path != game.url.standardizedFileURL.path
    }

    var clientReady: Bool { rosettaInstalled && clientConnected && !clientLocationDiffers }

    var isReady: Bool { gameReady && clientReady }

    /// Everything is set up, nothing about the app itself needs a word, and the client is FAF
    /// Native's own (the simple window's one button starts it).
    var simpleModePossible: Bool {
        isReady && installedClient != nil && macOSTooOld == nil && !Places.translocated
    }

    /// The full setup is on screen with everything ready (AppWindow, FAFNativeApp.swift). A failed
    /// save only means the full setup again next time.
    func noteSetupSeen() {
        guard config.setupSeen != true else { return }
        config.setupSeen = true
        try? config.save()
    }

    /// Setup & Settings in the simple window.
    func showFullSetup() { simpleMode = false }

    func use(_ folder: URL) {
        let checked = GameFolder(folder)
        guard checked.isValid else {
            alert = "That folder isn't a complete Forged Alliance installation. Missing: "
                + checked.missing.joined(separator: ", ")
            return
        }
        config.gamePath = folder.path
        do { try config.save() } catch { alert = "Couldn't save the setting: \(error.localizedDescription)" }
        refresh()
    }

    func chooseFolder() {
        let panel = NSOpenPanel()
        panel.message = "Choose the Forged Alliance folder (the one with bin, gamedata, sounds and movies in it)."
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        if let current = game?.url { panel.directoryURL = current.deletingLastPathComponent() }
        if panel.runModal() == .OK, let url = panel.url { use(url) }
    }

    private func measureGame() {
        gameSize = nil
        guard let source = game?.url, game?.isValid == true else { return }
        Task.detached(priority: .utility) {
            let size = GameCopy.size(of: source)
            await MainActor.run { if self.game?.url == source { self.gameSize = size } }
        }
    }

    var isCopying: Bool { copyFraction != nil }

    func startCopy() {
        guard let source = game?.url, game?.isValid == true, !isCopying else { return }
        let flag = CancelFlag()
        cancelFlag = flag
        copyFraction = 0
        copyText = "Preparing…"
        Task.detached(priority: .userInitiated) {
            do {
                try GameCopy.run(from: source, progress: { copied, total in
                    Task { @MainActor in
                        self.copyFraction = total > 0 ? Double(copied) / Double(total) : 1
                        self.copyText = String(format: "Copied %.1f of %.1f GB", Double(copied) / 1e9, Double(total) / 1e9)
                    }
                }, cancelled: { flag.isRaised })
                await MainActor.run {
                    self.copyFraction = nil
                    self.use(Places.copiedGame)
                }
            } catch is CancellationError {
                await MainActor.run { self.copyFraction = nil }
            } catch {
                await MainActor.run {
                    self.copyFraction = nil
                    self.alert = "The copy failed: \(error.localizedDescription)"
                }
            }
        }
    }

    func cancelCopy() { cancelFlag.raise() }

    /// The app's version line (Info.plist FAFNativeVersion, written by make_app.sh).
    var version: String { Bundle.main.object(forInfoDictionaryKey: "FAFNativeVersion") as? String ?? "development build" }

    /// The macOS version the game needs (the game app's LSMinimumSystemVersion), when this Mac is older.
    var macOSTooOld: String? {
        guard let plist = NSDictionary(contentsOf: Places.gameApp.appendingPathComponent("Contents/Info.plist")),
              let needed = plist["LSMinimumSystemVersion"] as? String else { return nil }
        let parts = needed.split(separator: ".").compactMap { Int($0) }
        let required = OperatingSystemVersion(majorVersion: parts.first ?? 0, minorVersion: parts.count > 1 ? parts[1] : 0, patchVersion: 0)
        return ProcessInfo.processInfo.isOperatingSystemAtLeast(required) ? nil : needed
    }

    /// The last line of the launcher's log: the latest launch or exit.
    var lastLaunch: String? {
        guard let text = try? String(contentsOf: Places.logs.appendingPathComponent("launch.log"), encoding: .utf8) else { return nil }
        return text.split(separator: "\n").last.map(String.init)
    }

    // MARK: The FAF client

    private func checkLatestRelease() {
        releaseProblem = nil
        Task {
            do {
                let release = try await ClientRelease.fetch()
                self.latestRelease = release
            } catch {
                self.releaseProblem = "Couldn't check for the latest FAF client: \(error.localizedDescription)"
            }
        }
    }

    var isInstallingClient: Bool { installFraction != nil }

    var clientUpdateAvailable: Bool {
        guard let installed = installedClient, let latest = latestRelease else { return false }
        return latest.version.compare(installed.clientVersion, options: .numeric) == .orderedDescending
    }

    func checkForClientUpdate() {
        guard let installed = installedClient, !checkingClientUpdate, !isInstallingClient else { return }
        checkingClientUpdate = true
        Task {
            defer { checkingClientUpdate = false }
            do {
                let release = try await ClientRelease.fetch()
                latestRelease = release
                releaseProblem = nil
                if release.version.compare(installed.clientVersion, options: .numeric) == .orderedDescending {
                    if clientRunning {
                        alert = "FAF client \(release.version) is available. Quit the client before installing the update."
                    } else {
                        pendingClientUpdate = release
                    }
                } else {
                    alert = "FAF client \(installed.clientVersion) is up to date."
                }
            } catch {
                releaseProblem = "Couldn't check for the latest FAF client: \(error.localizedDescription)"
                alert = releaseProblem
            }
        }
    }

    func installPendingClientUpdate() {
        guard let release = pendingClientUpdate else { return }
        pendingClientUpdate = nil
        installClient(release)
    }

    func installClient(_ requestedRelease: ClientRelease? = nil) {
        guard let release = requestedRelease ?? latestRelease, !isInstallingClient else { return }
        if installedClient != nil && clientRunning {
            alert = "Quit the FAF client before installing an update."
            return
        }
        installFraction = 0
        installText = "Starting…"
        installTask = Task {
            do {
                try await ClientInstaller.install(release) { text, fraction in
                    Task { @MainActor in
                        if self.installFraction != nil {
                            self.installText = text
                            self.installFraction = min(max(fraction, 0), 1)
                        }
                    }
                }
                self.installFraction = nil
            } catch {
                self.installFraction = nil
                if !Task.isCancelled && !(error is CancellationError) {
                    self.alert = "The FAF client wasn't installed: \(error.localizedDescription)"
                }
            }
            self.refresh()
        }
    }

    func cancelClientInstall() { installTask?.cancel() }

    /// The window calls this every few seconds: is a client running, has it written its settings
    /// (after the first login), and what do they say.
    func pollClient() {
        guard !isCopying, !isInstallingClient, clientBusy == nil else { return }
        clientRunning = ClientRunner.isRunning()
        clientPrefsExist = FileManager.default.fileExists(atPath: Places.clientPrefs.path)
        if clientPrefsExist || !clientRunning { clientLaunching = false }
        let info = ClientInfo.read()
        // The client rewrites client.prefs in place as it starts: a read in the middle keeps the last one.
        guard !info.prefsUnreadable else { return }
        if info != client { client = info }
        autoApplyClientSettingsIfReady()
        if simpleMode && !simpleModePossible { simpleMode = false }
    }

    /// Brings the running client's windows to the front (Show FAF Client in the simple window).
    func showClient() {
        for pid in ClientRunner.processIDs() {
            NSRunningApplication(processIdentifier: pid)?.activate(options: [.activateAllWindows])
        }
    }

    func stopClient() {
        clientBusy = "Stopping the FAF client…"
        Task.detached {
            let stopped = ClientRunner.stop()
            await MainActor.run {
                self.clientBusy = nil
                self.pollClient()
                if !stopped { self.alert = "The FAF client didn't quit." }
            }
        }
    }

    /// Stops the client if it runs, forgets its window placement, and starts FAF Native's own
    /// client again (another client is left for the player to start).
    func resetClientWindow() {
        clientBusy = clientRunning ? "Stopping the FAF client…" : "Resetting…"
        Task.detached {
            let stopped = ClientRunner.stop()
            await MainActor.run {
                self.clientBusy = nil
                guard stopped else {
                    self.alert = "The FAF client didn't quit, so its window wasn't reset."
                    self.pollClient()
                    return
                }
                do {
                    _ = try ClientSettings.resetWindow()
                    if self.installedClient != nil {
                        self.startClient()
                    } else {
                        self.alert = "Done: the FAF client's window opens at its default size and place the next time you start it."
                    }
                } catch {
                    self.alert = error.localizedDescription
                }
                self.pollClient()
            }
        }
    }

    func startClient() {
        guard let installed = installedClient else { return }
        clientLaunching = !clientPrefsExist
        do {
            try ClientRunner.start(installed)
            clientRunning = true
        } catch {
            clientLaunching = false
            alert = "The FAF client didn't start: \(error.localizedDescription)"
        }
    }

    /// Rosetta, through macOS's own administrator prompt.
    func installRosetta() {
        rosettaInstalling = true
        Task.detached {
            let process = Process()
            process.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
            process.arguments = ["-e", "do shell script \"/usr/sbin/softwareupdate --install-rosetta --agree-to-license\" with administrator privileges"]
            let failed: Bool
            do {
                try process.run()
                process.waitUntilExit()
                failed = process.terminationStatus != 0
            } catch {
                failed = true
            }
            await MainActor.run {
                self.rosettaInstalling = false
                self.refresh()
                if failed && !self.rosettaInstalled { self.alert = "Rosetta wasn't installed." }
            }
        }
    }

    /// Connects games started in the client to FAF Native and uses the selected game folder.
    func applyClientSettings() {
        do {
            let backup = try ClientSettings.apply(gamePath: game?.isValid == true ? game?.url.path : nil)
            client = ClientInfo.read()
            alert = "Done. The client's previous settings are saved as \(backup.lastPathComponent) in ~/.faforever."
        } catch {
            alert = error.localizedDescription
        }
    }

    /// The client writes its preferences after login and exit. Connect it once they are safe to edit.
    /// (Not for macos/tests/app/snapshot_setup.sh's look at this Mac, FAF_NATIVE_TEST_NO_AUTO_APPLY=1:
    /// a test copy of the app would take over the player's client.)
    private func autoApplyClientSettingsIfReady() {
        guard ProcessInfo.processInfo.environment["FAF_NATIVE_TEST_NO_AUTO_APPLY"] != "1" else { return }
        guard !clientRunning, clientPrefsExist, clientBusy == nil, !Places.translocated,
              let game, game.isValid else { return }
        let connected: Bool
        switch client.hookup {
        case .thisApp, .handsOver: connected = true
        default: connected = false
        }
        let locationMatches = client.installationPath.map {
            URL(fileURLWithPath: $0).standardizedFileURL.path == game.url.standardizedFileURL.path
        } ?? false
        guard !connected || !locationMatches else { return }
        let attributes = try? FileManager.default.attributesOfItem(atPath: Places.clientPrefs.path)
        let modified = (attributes?[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0
        let attempt = "\(modified):\(game.url.standardizedFileURL.path):\(Places.launcher.path)"
        guard lastAutoApplyAttempt != attempt else { return }
        lastAutoApplyAttempt = attempt
        do {
            _ = try ClientSettings.apply(gamePath: game.url.path)
            client = ClientInfo.read()
        } catch {
            alert = "FAF Native couldn't connect the client automatically: \(error.localizedDescription)"
        }
    }

    func copyToClipboard(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}
