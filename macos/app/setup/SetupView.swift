// FAF Native's setup and status window (Setup.swift has the model). Three sections: the Forged
// Alliance files (found, chosen, or copied into the app's folder), the FAF client (its data, and
// whether it starts games through this app), and this app (version, macOS, last launch, logs).
import AppKit
import SwiftUI

/// A status line: a coloured symbol and a short statement, with optional detail under it.
struct StatusRow: View {
    enum Kind { case good, warning, problem, info }
    let kind: Kind
    let title: String
    var detail: String?

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Image(systemName: symbol).foregroundStyle(color)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                if let detail {
                    Text(detail).font(.callout).foregroundStyle(.secondary).textSelection(.enabled)
                }
            }
        }
    }

    private var symbol: String {
        switch kind {
        case .good: return "checkmark.circle.fill"
        case .warning: return "exclamationmark.triangle.fill"
        case .problem: return "xmark.octagon.fill"
        case .info: return "info.circle.fill"
        }
    }

    private var color: Color {
        switch kind {
        case .good: return .green
        case .warning: return .orange
        case .problem: return .red
        case .info: return .blue
        }
    }
}

/// Text to paste somewhere else, with a Copy button.
struct CopyField: View {
    @EnvironmentObject private var model: SetupModel
    let text: String

    var body: some View {
        HStack {
            Text(text)
                .font(.system(.callout, design: .monospaced))
                .textSelection(.enabled)
                .lineLimit(3)
                .frame(maxWidth: .infinity, alignment: .leading)
            Button("Copy") { model.copyToClipboard(text) }
        }
    }
}

struct SetupView: View {
    @EnvironmentObject private var model: SetupModel
    @State private var confirmApply = false
    @State private var confirmStop = false
    @State private var confirmReset = false

    var body: some View {
        Form {
            gameSection
            clientSection
            appSection
        }
        .formStyle(.grouped)
        .confirmationDialog("Change the FAF client's settings?", isPresented: $confirmApply) {
            Button("Apply") { model.applyClientSettings() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("FAF Native sets the client's executable decorator to its launcher and its Forged Alliance location to the folder chosen above. "
                + "Everything else stays as it is, and the current settings file is kept as a backup.")
        }
        .confirmationDialog("Stop the FAF client?", isPresented: $confirmStop) {
            Button("Stop FAF Client", role: .destructive) { model.stopClient() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("A game in progress ends too.")
        }
        .confirmationDialog("Reset the FAF client's window?", isPresented: $confirmReset) {
            Button("Reset Window") { model.resetClientWindow() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text((model.clientRunning ? "FAF Native quits the client (a game in progress ends too), " : "FAF Native ")
                + "puts its window back to the default size and place on the main screen"
                + (model.installedClient != nil ? ", and starts it again." : ".")
                + " Its other settings stay as they are; the settings file is kept as a backup.")
        }
        .onReceive(Timer.publish(every: 3, on: .main, in: .common).autoconnect()) { _ in model.pollClient() }
        .alert("FAF Native", isPresented: Binding(get: { model.alert != nil }, set: { if !$0 { model.alert = nil } })) {
            Button("OK") { model.alert = nil }
        } message: {
            Text(model.alert ?? "")
        }
    }

    // MARK: Forged Alliance files

    private var gameSection: some View {
        Section {
            if let game = model.game {
                if game.isValid {
                    StatusRow(kind: .good, title: game.isAppCopy ? "Using FAF Native's own copy" : "Found", detail: game.url.path)
                } else {
                    StatusRow(kind: .problem, title: "This folder is incomplete",
                              detail: game.url.path + "\nMissing: " + game.missing.joined(separator: ", "))
                }
            } else {
                StatusRow(kind: .problem, title: "Not found",
                          detail: "FAF Native looked in the FAF client's settings and in CrossOver, wrapper apps and Wine. "
                              + "Choose the folder that holds bin, gamedata, sounds and movies.")
            }
            HStack {
                Button("Choose Folder…") { model.chooseFolder() }
                if let game = model.game {
                    Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([game.url]) }
                }
                if !model.others.isEmpty {
                    Menu("Other Installations") {
                        ForEach(model.others) { other in
                            Button(other.url.path) { model.use(other.url) }
                        }
                    }
                    .fixedSize()
                }
            }
            .disabled(model.isCopying)
            copyControls
        } header: {
            Text("Forged Alliance files")
        } footer: {
            Text("Your own copy of Supreme Commander: Forged Alliance (Steam, GOG or disc). "
                + "FAF Native uses it where it is, or keeps its own copy so the original can go.")
        }
    }

    @ViewBuilder private var copyControls: some View {
        if let fraction = model.copyFraction {
            VStack(alignment: .leading, spacing: 6) {
                ProgressView(value: fraction)
                HStack {
                    Text(model.copyText).font(.callout).foregroundStyle(.secondary)
                    Spacer()
                    Button("Cancel") { model.cancelCopy() }
                }
            }
        } else if let game = model.game, game.isValid, !game.isAppCopy {
            HStack {
                Text(model.gameSize.map { String(format: "%.1f GB", Double($0) / 1e9) } ?? "Measuring…")
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Copy into FAF Native") { model.startCopy() }
                    .disabled(model.gameSize == nil)
            }
        }
    }

    // MARK: FAF client

    /// Top to bottom in the order to do things: Rosetta, the client (install, start, log in, quit),
    /// connecting it to FAF Native, then the first game (which brings FAF's game files).
    private var clientSection: some View {
        Section {
            rosettaRows
            installRows
            connectRows
            locationRows
            gameDataRow
        } header: {
            Text("FAF client")
        } footer: {
            Text("FAF's own client for Mac users: its Unix build on a Java runtime for Intel Macs (under Rosetta) with the macOS JavaFX libraries. "
                + "It keeps its settings and downloads in ~/.faforever as usual.")
        }
    }

    @ViewBuilder private var rosettaRows: some View {
        if !model.rosettaInstalled {
            StatusRow(kind: .warning, title: "Rosetta isn't installed",
                      detail: "The FAF client's Java runtime is made for Intel Macs; Rosetta runs it on Apple silicon.")
            HStack {
                Spacer()
                Button(model.rosettaInstalling ? "Installing…" : "Install Rosetta…") { model.installRosetta() }
                    .disabled(model.rosettaInstalling)
            }
        }
    }

    @ViewBuilder private var installRows: some View {
        if let fraction = model.installFraction {
            VStack(alignment: .leading, spacing: 6) {
                ProgressView(value: fraction)
                HStack {
                    Text(model.installText).font(.callout).foregroundStyle(.secondary)
                    Spacer()
                    Button("Cancel") { model.cancelClientInstall() }
                }
            }
        } else if let installed = model.installedClient {
            StatusRow(kind: .good, title: "FAF client \(installed.clientVersion) is set up",
                      detail: "Java \(installed.javaVersion) and JavaFX \(installed.javafxVersion) for macOS, in \(Places.clientInstall.path)")
            HStack {
                if model.clientUpdateAvailable, let latest = model.latestRelease {
                    Button("Update to \(latest.version)") { model.installClient() }
                        .disabled(model.clientRunning || model.clientBusy != nil)
                }
                Spacer()
                clientControls
            }
        } else {
            if model.client.hasData || model.clientPrefsExist {
                StatusRow(kind: .info, title: "Your FAF client was set up outside FAF Native",
                          detail: "FAF Native can also keep a copy of its own and update it. " + releaseSummary)
            } else {
                StatusRow(kind: .info, title: "First: let FAF Native set up the FAF client", detail: releaseSummary)
            }
            HStack {
                // Stop and Reset Window work for a client set up elsewhere too.
                clientControls
                Spacer()
                Button("Download and Install") { model.installClient() }
                    .disabled(model.latestRelease == nil || model.clientBusy != nil)
            }
        }
    }

    /// Reset Window, Stop and Start, for whichever client runs (Start for FAF Native's own only).
    @ViewBuilder private var clientControls: some View {
        if let busy = model.clientBusy {
            ProgressView().controlSize(.small)
            Text(busy).font(.callout).foregroundStyle(.secondary)
        } else {
            if model.clientPrefsExist {
                Button("Reset Window…") { confirmReset = true }
                    .help("Puts the FAF client's window back to its default size and place on the main screen")
            }
            if model.clientRunning {
                Button("Stop FAF Client") { confirmStop = true }
                Button("FAF Client Is Running") {}
                    .disabled(true)
            } else if model.installedClient != nil {
                Button("Start FAF Client") { model.startClient() }
                    .disabled(!model.rosettaInstalled)
            }
        }
    }

    private var releaseSummary: String {
        guard let release = model.latestRelease else { return model.releaseProblem ?? "Checking for the latest version…" }
        let megabytes = Int((Double(release.client.size + release.java.size) + 47e6) / 1e6)
        return "Version \(release.version), with Java \(release.javaVersion) and the macOS JavaFX libraries: about \(megabytes) MB "
            + "from GitHub, Adoptium and Maven Central, each checked against the checksum its source publishes."
    }

    private var connected: Bool {
        switch model.client.hookup {
        case .thisApp, .handsOver: return true
        case .noClient, .none, .otherLauncher: return false
        }
    }

    /// Connecting the client to FAF Native: its executable decorator setting.
    @ViewBuilder private var connectRows: some View {
        switch model.client.hookup {
        case .thisApp:
            StatusRow(kind: .good, title: "The client starts games with FAF Native")
        case .handsOver(let launcher):
            StatusRow(kind: .good, title: "The client starts games through a launcher that hands over to FAF Native", detail: launcher)
        case .noClient:
            if model.installedClient != nil {
                StatusRow(kind: .info, title: "Next: start the FAF client, log in, then quit it",
                          detail: "Don't start a game yet. Once you've logged in, FAF Native can connect the client so its games start FAF Native.")
            } else {
                StatusRow(kind: .info, title: "A FAF client has to start its games with FAF Native",
                          detail: "Its executable decorator setting (Forged Alliance) is this line; FAF Native can set it after the client's first start.")
                CopyField(text: Places.launcherLine)
            }
        case .none, .otherLauncher:
            if case .otherLauncher(let other) = model.client.hookup {
                StatusRow(kind: .warning, title: "Next: connect the client to FAF Native", detail: "It starts games with another program: \(other)")
            } else {
                StatusRow(kind: .warning, title: "Next: connect the client to FAF Native",
                          detail: "Until then, starting a game in the client tries to start the Windows game.")
            }
            applyRow
            Text("Or set it yourself: in the client's settings, the executable decorator for Forged Alliance is this line.")
                .font(.callout).foregroundStyle(.secondary)
            CopyField(text: Places.launcherLine)
        }
    }

    private var locationDiffers: Bool {
        guard let game = model.game, game.isValid, let clientPath = model.client.installationPath else { return false }
        return URL(fileURLWithPath: clientPath).standardizedFileURL.path != game.url.standardizedFileURL.path
    }

    /// When the client's Forged Alliance location differs from the folder chosen here.
    @ViewBuilder private var locationRows: some View {
        if locationDiffers, let game = model.game, let clientPath = model.client.installationPath {
            StatusRow(kind: .warning, title: "The client looks for Forged Alliance somewhere else", detail: clientPath)
            if connected { applyRow }
            Text("Or set the client's Forged Alliance location yourself to:").font(.callout).foregroundStyle(.secondary)
            CopyField(text: game.url.path)
        }
    }

    @ViewBuilder private var applyRow: some View {
        if model.clientPrefsExist {
            HStack {
                Text(model.clientRunning ? "Quit the FAF client first (Stop FAF Client above), then let FAF Native do it."
                                         : "FAF Native can do it for you, keeping a backup of the client's settings.")
                    .font(.callout)
                Spacer()
                Button("Apply for Me…") { confirmApply = true }
                    .disabled(model.clientRunning || model.clientBusy != nil)
            }
        } else {
            Text("Start the FAF client and log in once; FAF Native can then do this for you.")
                .font(.callout).foregroundStyle(.secondary)
        }
    }

    /// FAF's game files come with the first game the client starts.
    @ViewBuilder private var gameDataRow: some View {
        if model.client.hasData {
            let version = [model.client.gameType, model.client.gameVersion].compactMap { $0 }.joined(separator: " ")
            StatusRow(kind: .good, title: "FAF's game files are in place",
                      detail: version.isEmpty ? Places.clientData.path : "\(Places.clientData.path) (\(version))")
        } else {
            StatusRow(kind: .info, title: "Last: FAF's game files come with your first game",
                      detail: connected
                          ? "Start or join a game in the FAF client. The first time, it downloads FAF's files into ~/.faforever (a few minutes), then the game opens."
                          : "Once the client is connected to FAF Native, start or join a game in it: the first time, it downloads FAF's files into ~/.faforever, then the game opens.")
        }
    }

    // MARK: This app

    private var appSection: some View {
        Section {
            if let needed = model.macOSTooOld {
                StatusRow(kind: .problem, title: "This build of the game needs macOS \(needed) or later")
            }
            LabeledContent("Version", value: model.version)
            if let last = model.lastLaunch {
                LabeledContent("Last launch") {
                    Text(last).font(.callout).lineLimit(2).truncationMode(.middle).textSelection(.enabled)
                }
            }
            HStack {
                Button("Open Logs Folder") {
                    try? FileManager.default.createDirectory(at: Places.logs, withIntermediateDirectories: true)
                    NSWorkspace.shared.open(Places.logs)
                }
                Spacer()
                Button("Check Again") { model.refresh() }
                    .disabled(model.isCopying)
            }
        } header: {
            Text("FAF Native")
        }
    }
}
