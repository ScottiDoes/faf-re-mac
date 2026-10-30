// FAF Native's setup window (Setup.swift has the model). It opens the first unfinished step:
// find Forged Alliance, connect the FAF client, then play. App details live in the toolbar.
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
    private enum Step: Equatable { case game, client, play }

    @EnvironmentObject private var model: SetupModel
    @State private var confirmApply = false
    @State private var confirmStop = false
    @State private var confirmReset = false
    @State private var expandedGame = true
    @State private var expandedClient = false
    @State private var expandedPlay = false
    @State private var showAppDetails = false

    private var gameReady: Bool { model.game?.isValid == true }
    private var clientReady: Bool { model.rosettaInstalled && connected && !locationDiffers }
    private var currentStep: Step {
        if !gameReady { return .game }
        if !clientReady { return .client }
        return .play
    }

    private func expansion(for step: Step) -> Binding<Bool> {
        Binding(get: {
            switch step {
            case .game: expandedGame
            case .client: expandedClient
            case .play: expandedPlay
            }
        }, set: { isExpanded in
            withAnimation(.easeInOut(duration: 0.3)) {
                switch step {
                case .game: expandedGame = isExpanded
                case .client: expandedClient = isExpanded
                case .play: expandedPlay = isExpanded
                }
            }
        })
    }

    private func focus(on step: Step) {
        expandedGame = step == .game
        expandedClient = step == .client
        expandedPlay = step == .play
    }

    private func stepLabel(_ title: String, complete: Bool) -> some View {
        HStack {
            Text(title).font(.headline)
            Spacer()
            if complete { Text("Done").font(.caption).foregroundStyle(.green) }
        }
    }

    private func stepHeader(_ step: Step, title: String, complete: Bool) -> some View {
        let isExpanded = expansion(for: step).wrappedValue
        return Button {
            expansion(for: step).wrappedValue = !isExpanded
        } label: {
            HStack(spacing: 8) {
                Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                    .font(.system(size: 11, weight: .semibold))
                    .frame(width: 14)
                stepLabel(title, complete: complete)
            }
            .frame(maxWidth: .infinity, minHeight: 40, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(title)
        .accessibilityValue(isExpanded ? "Expanded" : "Collapsed")
    }

    private func stepCard<Content: View>(_ step: Step, title: String, complete: Bool,
                                         @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            stepHeader(step, title: title, complete: complete)
            if expansion(for: step).wrappedValue {
                Divider()
                content()
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 12))
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                if Places.translocated { translocationSection }
                if let needed = model.macOSTooOld {
                    StatusRow(kind: .problem, title: "This build of the game needs macOS \(needed) or later")
                        .padding(12)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 12))
                }
                gameSection
                clientSection
                playSection
            }
            .padding(20)
            .frame(maxWidth: .infinity)
        }
        .background { SetupWindowBackground().ignoresSafeArea() }
        .toolbar {
            ToolbarItem(placement: .primaryAction) { infoButton }
        }
        .onAppear { focus(on: currentStep) }
        .onChange(of: currentStep) { _, step in
            withAnimation(.easeInOut(duration: 0.3)) { focus(on: step) }
        }
        .confirmationDialog("Reconnect the FAF client?", isPresented: $confirmApply) {
            Button("Reapply") { model.applyClientSettings() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Games you start in the FAF client will open through FAF Native using the Forged Alliance folder selected in Step 1. "
                + "Your current client settings will be backed up first.")
        }
        .confirmationDialog("Update the FAF client?", isPresented: Binding(
            get: { model.pendingClientUpdate != nil },
            set: { if !$0 { model.pendingClientUpdate = nil } }
        )) {
            Button("Install Update") { model.installPendingClientUpdate() }
            Button("Cancel", role: .cancel) { model.pendingClientUpdate = nil }
        } message: {
            if let release = model.pendingClientUpdate {
                Text("FAF client \(release.version) is available. Install it now?")
            }
        }
        .confirmationDialog("Quit the FAF client?", isPresented: $confirmStop) {
            Button("Quit FAF Client", role: .destructive) { model.stopClient() }
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

    // MARK: Temporary copy

    private var translocationSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            StatusRow(kind: .problem, title: "macOS is running FAF Native from a temporary copy",
                      detail: "The app still carries the download flag, so macOS runs it from a hidden copy whose path changes, "
                          + "and the FAF client would lose track of it. Quit FAF Native, move it into Applications with Finder, "
                          + "run this command in Terminal once, and open it again:")
            CopyField(text: Places.clearFlagCommand)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 12))
    }

    // MARK: Forged Alliance files

    private var gameSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            stepCard(.game, title: "Step 1 — Forged Alliance files", complete: gameReady) {
                if let game = model.game {
                    if game.isValid {
                        StatusRow(kind: .good, title: game.isAppCopy ? "Using FAF Native's own copy" : "Found", detail: game.url.path)
                        if let guarded = game.guardedFolder {
                            StatusRow(kind: .warning, title: "This folder is in \(guarded)",
                                      detail: "macOS asks permission before apps read Desktop, Documents and Downloads, and iCloud's Desktop & Documents "
                                          + "sync may keep only placeholders of big files there, which can stall a game while it loads. "
                                          + "Use Copy into FAF Native below, or move the folder (for example to a Games folder in your home folder) and choose it again.")
                        }
                    } else {
                        StatusRow(kind: .problem, title: "This folder is incomplete",
                                  detail: game.url.path + "\nMissing: " + game.missing.joined(separator: ", "))
                    }
                } else {
                    StatusRow(kind: .problem, title: "Not found",
                              detail: "FAF Native looked in the FAF client's settings and in CrossOver, wrapper apps and Wine. "
                                  + "Choose the folder that holds bin, gamedata, sounds and movies.")
                }
                Divider()
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
            }
            if expandedGame {
                Text("Your own copy of Supreme Commander: Forged Alliance (Steam, GOG or disc). "
                    + "FAF Native uses it where it is, or keeps its own copy so the original can go.")
                    .font(.callout).foregroundStyle(.secondary)
                    .padding(.horizontal, 12)
            }
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

    /// Install, start and log in, quit, then connect the client to FAF Native.
    private var clientSection: some View {
        stepCard(.client, title: "Step 2 — FAF client", complete: clientReady) {
            rosettaRows
            installRows
            Divider()
            startClientRow
            Divider()
            followUpControls
            locationRows
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
        } else {
            if model.client.hasData || model.clientPrefsExist {
                StatusRow(kind: .info, title: "Your FAF client was set up outside FAF Native",
                          detail: "FAF Native can also keep a copy of its own and update it. " + releaseSummary)
            } else {
                StatusRow(kind: .info, title: "Let FAF Native set up the FAF client", detail: releaseSummary)
            }
            HStack {
                Button("Download and Install") { model.installClient() }
                    .disabled(model.latestRelease == nil || model.clientBusy != nil)
            }
        }
    }

    private var startClientRow: some View {
        HStack(spacing: 12) {
            Button { model.startClient() } label: {
                Text("Start FAF Client").opacity(model.clientLaunching ? 0 : 1)
            }
                .overlay {
                    if model.clientLaunching { ProgressView().controlSize(.small) }
                }
                .disabled(model.installedClient == nil || !model.rosettaInstalled || model.clientRunning
                          || model.clientBusy != nil || model.isInstallingClient)
            Text("Login to the client, then immediately exit.")
                .font(.callout).foregroundStyle(.secondary)
            Spacer(minLength: 0)
        }
    }

    private var followUpControls: some View {
        HStack(spacing: 8) {
            Button("Quit FAF Client") { confirmStop = true }
                .disabled(!model.clientRunning || model.clientBusy != nil)
            Button("Reset Window…") { confirmReset = true }
                .disabled(!model.clientPrefsExist || model.clientBusy != nil)
                .help("Puts the FAF client's window back to its default size and place on the main screen")
            Button("Reapply…") { confirmApply = true }
                .disabled(!model.clientPrefsExist || model.clientRunning || model.clientBusy != nil
                          || Places.translocated || !gameReady)
                .help("Reconnects games started in the FAF client to FAF Native and backs up its settings")
            Button("Check for Updates") { model.checkForClientUpdate() }
                .disabled(model.installedClient == nil || model.checkingClientUpdate
                          || model.isInstallingClient || model.clientBusy != nil)
            Spacer(minLength: 0)
            if model.checkingClientUpdate { ProgressView().controlSize(.small) }
            if let busy = model.clientBusy {
                ProgressView().controlSize(.small)
                Text(busy).font(.callout).foregroundStyle(.secondary)
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

    private var locationDiffers: Bool {
        guard let game = model.game, game.isValid, let clientPath = model.client.installationPath else { return false }
        return URL(fileURLWithPath: clientPath).standardizedFileURL.path != game.url.standardizedFileURL.path
    }

    /// When the client's Forged Alliance location differs from the folder chosen here.
    @ViewBuilder private var locationRows: some View {
        if locationDiffers, let game = model.game, let clientPath = model.client.installationPath {
            StatusRow(kind: .warning, title: "The client looks for Forged Alliance somewhere else", detail: clientPath)
            Text("Or set the client's Forged Alliance location yourself to:").font(.callout).foregroundStyle(.secondary)
            CopyField(text: game.url.path)
        }
    }

    // MARK: Play

    private var playSection: some View {
        stepCard(.play, title: "Step 3 — Play the game", complete: false) {
            HStack {
                Spacer()
                Button("Start FAF Client") { model.startClient() }
                    .disabled(model.installedClient == nil || !model.rosettaInstalled
                              || model.clientRunning || model.clientBusy != nil)
                Spacer()
            }
        }
    }

    // MARK: App details

    @ViewBuilder private var infoButton: some View {
        if #available(macOS 26.0, *) {
            detailsButton.buttonStyle(.glass)
        } else {
            detailsButton.buttonStyle(.borderless)
        }
    }

    private var detailsButton: some View {
        Button { showAppDetails.toggle() } label: {
            Image(systemName: "info.circle")
                .imageScale(.large)
                .accessibilityLabel("FAF Native details")
        }
        .help("FAF Native details")
        .popover(isPresented: $showAppDetails) { appDetailsPopover }
    }

    private var appDetailsPopover: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("FAF Native").font(.headline)
            LabeledContent("Version") {
                Text(model.version).textSelection(.enabled)
            }
            if let last = model.lastLaunch {
                LabeledContent("Last launch") {
                    Text(last).textSelection(.enabled)
                }
            }
            Divider()
            HStack {
                Button("Open Logs Folder") {
                    try? FileManager.default.createDirectory(at: Places.logs, withIntermediateDirectories: true)
                    NSWorkspace.shared.open(Places.logs)
                }
                Spacer()
                Button("Check Again") { model.refresh() }
                    .disabled(model.isCopying)
            }
        }
        .padding(20)
        .frame(width: 390)
    }
}
