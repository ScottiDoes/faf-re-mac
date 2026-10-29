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

    private var clientSection: some View {
        Section {
            rosettaRows
            installRows
            if model.client.hasData {
                let version = [model.client.gameType, model.client.gameVersion].compactMap { $0 }.joined(separator: " ")
                StatusRow(kind: .good, title: "FAF's game files are in place",
                          detail: version.isEmpty ? Places.clientData.path : "\(Places.clientData.path) (\(version))")
            } else {
                StatusRow(kind: .problem, title: "No FAF game files yet",
                          detail: (model.installedClient == nil ? "Install the FAF client" : "Start the FAF client")
                              + ", log in, and start any game once: the client downloads FAF's files into ~/.faforever.")
            }
            hookupRows
            locationRows
            applyRow
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
                        .disabled(model.clientRunning)
                }
                Spacer()
                Button(model.clientRunning ? "FAF Client Is Running" : "Start FAF Client") { model.startClient() }
                    .disabled(model.clientRunning || !model.rosettaInstalled)
            }
        } else {
            if model.client.hasData || model.clientPrefsExist {
                StatusRow(kind: .info, title: "Your FAF client was set up outside FAF Native",
                          detail: "FAF Native can also keep a copy of its own and update it. " + releaseSummary)
            } else {
                StatusRow(kind: .info, title: "FAF Native can set up the FAF client for you", detail: releaseSummary)
            }
            HStack {
                Spacer()
                Button("Download and Install") { model.installClient() }
                    .disabled(model.latestRelease == nil)
            }
        }
    }

    private var releaseSummary: String {
        guard let release = model.latestRelease else { return model.releaseProblem ?? "Checking for the latest version…" }
        let megabytes = Int((Double(release.client.size + release.java.size) + 47e6) / 1e6)
        return "Version \(release.version), with Java \(release.javaVersion) and the macOS JavaFX libraries: about \(megabytes) MB "
            + "from GitHub, Adoptium and Maven Central, each checked against the checksum its source publishes."
    }

    /// The client's settings need changing: its launcher, or its Forged Alliance location.
    private var settingsNeeded: Bool {
        switch model.client.hookup {
        case .none, .otherLauncher: return true
        case .noClient, .thisApp, .handsOver: break
        }
        guard let game = model.game, game.isValid, let clientPath = model.client.installationPath else { return false }
        return URL(fileURLWithPath: clientPath).standardizedFileURL.path != game.url.standardizedFileURL.path
    }

    @ViewBuilder private var applyRow: some View {
        if settingsNeeded {
            if model.clientPrefsExist {
                HStack {
                    Text(model.clientRunning ? "Quit the FAF client, and FAF Native can set these for you."
                                             : "Or let FAF Native set these in the client's settings (a backup is kept).")
                        .font(.callout).foregroundStyle(.secondary)
                    Spacer()
                    Button("Apply for Me…") { confirmApply = true }
                        .disabled(model.clientRunning)
                }
            } else {
                Text("Start the FAF client once and log in; FAF Native can then set these for you.")
                    .font(.callout).foregroundStyle(.secondary)
            }
        }
    }

    @ViewBuilder private var hookupRows: some View {
        switch model.client.hookup {
        case .thisApp:
            StatusRow(kind: .good, title: "The client starts games with FAF Native")
        case .handsOver(let launcher):
            StatusRow(kind: .good, title: "The client starts games through a launcher that hands over to FAF Native", detail: launcher)
        case .noClient:
            StatusRow(kind: .info, title: model.installedClient == nil
                          ? "Once the client is installed, set its executable decorator to this line:"
                          : "After the client's first start, set its executable decorator to this line (or let FAF Native do it then):")
            CopyField(text: Places.launcherLine)
        case .none, .otherLauncher:
            if case .otherLauncher(let other) = model.client.hookup {
                StatusRow(kind: .warning, title: "The client starts games with another program", detail: other)
            } else {
                StatusRow(kind: .warning, title: "The client still starts the Windows game")
            }
            Text("In the FAF client's settings, set the executable decorator for Forged Alliance to this line, then start games from the client as usual:")
                .font(.callout)
            CopyField(text: Places.launcherLine)
        }
    }

    /// When the client's Forged Alliance location differs from the folder chosen here.
    @ViewBuilder private var locationRows: some View {
        if let game = model.game, game.isValid,
           let clientPath = model.client.installationPath,
           URL(fileURLWithPath: clientPath).standardizedFileURL.path != game.url.standardizedFileURL.path {
            StatusRow(kind: .warning, title: "The client looks for Forged Alliance somewhere else", detail: clientPath)
            Text("Set the client's Forged Alliance location to:").font(.callout)
            CopyField(text: game.url.path)
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
