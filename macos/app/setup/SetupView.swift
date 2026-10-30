// FAF Native's setup window (Setup.swift has the model, Theme.swift the look). Three step cards
// (Forged Alliance files, the FAF client, play) over one panel whose pages slide sideways; the
// window opens on the first unfinished step and moves on as each is done. App details live in the
// toolbar (AppDetailsButton, also in the simple window).
import AppKit
import SwiftUI

struct SetupView: View {
    private enum Step: Int, CaseIterable { case game, client, play }

    @EnvironmentObject private var model: SetupModel
    /// The step to show first (1-3) instead of the first unfinished one: snapshot_setup.swift's pages.
    var startStep: Int?
    @State private var shown: Step = .game
    @State private var confirmApply = false
    @State private var confirmStop = false
    @State private var confirmReset = false

    private var gameReady: Bool { model.gameReady }
    private var clientReady: Bool { model.clientReady }
    private var connected: Bool { model.clientConnected }
    private var locationDiffers: Bool { model.clientLocationDiffers }
    private var currentStep: Step {
        if !gameReady { return .game }
        if !clientReady { return .client }
        return .play
    }

    private func go(to step: Step) {
        withAnimation(Theme.slide) { shown = step }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            banners
            stepCards
            stepMarker
            pages
        }
        .padding(.horizontal, 24)
        .padding(.top, 4)
        .padding(.bottom, 24)
        .foregroundStyle(Theme.text)
        .background { SetupWindowBackground().ignoresSafeArea() }
        .preferredColorScheme(.dark)
        .toolbar {
            ToolbarItem(placement: .primaryAction) { AppDetailsButton() }
        }
        .onAppear { shown = startStep.flatMap { Step(rawValue: $0 - 1) } ?? currentStep }
        .onChange(of: currentStep) { _, step in go(to: step) }
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

    // MARK: Problems with the app itself

    @ViewBuilder private var banners: some View {
        if Places.translocated {
            VStack(alignment: .leading, spacing: 10) {
                StatusRow(kind: .problem, title: "macOS is running FAF Native from a temporary copy",
                          detail: "The app still carries the download flag, so macOS runs it from a hidden copy whose path changes, "
                              + "and the FAF client would lose track of it. Quit FAF Native, move it into Applications with Finder, "
                              + "run this command in Terminal once, and open it again:")
                CopyField(text: Places.clearFlagCommand)
            }
            .padding(.bottom, 12)
        }
        if let needed = model.macOSTooOld {
            StatusRow(kind: .problem, title: "This build of the game needs macOS \(needed) or later")
                .padding(.bottom, 12)
        }
    }

    // MARK: Step cards

    private var stepCards: some View {
        HStack(spacing: 12) {
            StepCard(number: 1, title: "Forged Alliance files", status: gameStatus.text, statusColor: gameStatus.color,
                     badge: gameReady ? .done : .number, selected: shown == .game, waiting: false) { go(to: .game) }
            StepCard(number: 2, title: "FAF client", status: clientStatus.text, statusColor: clientStatus.color,
                     badge: clientReady ? .done : .number, selected: shown == .client, waiting: !gameReady && !clientReady) { go(to: .client) }
            StepCard(number: 3, title: "Play", status: playStatus.text, statusColor: playStatus.color,
                     badge: gameReady && clientReady ? .play : .number, selected: shown == .play,
                     waiting: !(gameReady && clientReady)) { go(to: .play) }
        }
    }

    /// The bar under the card in view.
    private var stepMarker: some View {
        GeometryReader { geometry in
            let card = (geometry.size.width - 24) / 3
            Capsule()
                .fill(Theme.accent)
                .frame(width: 40, height: 4)
                .offset(x: CGFloat(shown.rawValue) * (card + 12) + (card - 40) / 2, y: 7)
        }
        .frame(height: 18)
        .accessibilityHidden(true)
    }

    private var gameStatus: (text: String, color: Color) {
        if let fraction = model.copyFraction { return ("Copying… \(Int(fraction * 100))%", Theme.secondary) }
        guard let game = model.game else { return ("Not found", Theme.redText) }
        guard game.isValid else { return ("Incomplete", Theme.redText) }
        let name = game.isAppCopy ? "FAF Native's copy" : "Found"
        return (model.gameSize.map { "\(name) · \(gigabytes($0))" } ?? name, Theme.secondary)
    }

    private var clientStatus: (text: String, color: Color) {
        if clientReady {
            return (model.installedClient.map { "\($0.clientVersion) · connected" } ?? "Connected", Theme.secondary)
        }
        if let fraction = model.installFraction { return ("Installing… \(Int(fraction * 100))%", Theme.secondary) }
        if !model.rosettaInstalled { return ("Needs Rosetta", Theme.orange) }
        if model.installedClient == nil && !model.client.hasData && !model.clientPrefsExist {
            return (gameReady ? "Not installed" : "Waits for step 1", Theme.tertiary)
        }
        if locationDiffers { return ("Looks for the game elsewhere", Theme.orange) }
        return ("Sign in once, then quit", Theme.tertiary)
    }

    private var playStatus: (text: String, color: Color) {
        switch (gameReady, clientReady) {
        case (true, true): return ("Ready to play", Theme.accentText)
        case (false, false): return ("Waits for steps 1 and 2", Theme.tertiary)
        case (false, true): return ("Waits for step 1", Theme.tertiary)
        case (true, false): return ("Waits for step 2", Theme.tertiary)
        }
    }

    // MARK: Pages

    private var pages: some View {
        GeometryReader { geometry in
            HStack(alignment: .top, spacing: 0) {
                page(.game, width: geometry.size.width, title: "Forged Alliance files",
                     text: "Your own copy of Supreme Commander: Forged Alliance (Steam, GOG or disc). "
                         + "FAF Native uses it where it is, or keeps its own copy so the original can go.",
                     back: nil, next: ("FAF client", gameReady)) { gamePage }
                page(.client, width: geometry.size.width, title: "FAF client",
                     text: "The FAF client is where you find games and chat, and where maps and mods come from. "
                         + "FAF Native keeps its own copy up to date and connects it to the native game.",
                     back: .game, next: ("Play", clientReady)) { clientPage }
                page(.play, width: geometry.size.width, title: "Play",
                     text: gameReady && clientReady
                         ? "Everything is set up. Start the FAF client, then host or join a game: it opens in FAF Native."
                         : "Finish steps 1 and 2 first: the Forged Alliance files, and a FAF client connected to FAF Native.",
                     back: .client, next: nil) { playPage }
            }
            .frame(width: geometry.size.width * 3, height: geometry.size.height, alignment: .topLeading)
            .offset(x: -CGFloat(shown.rawValue) * geometry.size.width)
        }
        .background(Theme.panel)
        .clipShape(RoundedRectangle(cornerRadius: 14))
        .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(Theme.line))
    }

    private func page<Content: View>(_ step: Step, width: CGFloat, title: String, text: String, back: Step?,
                                     next: (title: String, enabled: Bool)?, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    VStack(alignment: .leading, spacing: 0) {
                        Text("Step \(step.rawValue + 1) of 3").caption()
                        Text(title)
                            .font(.system(size: 22, weight: .bold))
                            .padding(.top, 4)
                            .padding(.bottom, 6)
                        Text(text)
                            .font(.system(size: 13.5))
                            .lineSpacing(3)
                            .foregroundStyle(Theme.secondary)
                            .frame(maxWidth: 620, alignment: .leading)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    content()
                }
                .padding(.horizontal, 30)
                .padding(.top, 26)
                .padding(.bottom, 18)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            VStack(spacing: 0) {
                Rectangle().fill(Color.white.opacity(0.07)).frame(height: 1)
                HStack {
                    if let back {
                        Button { go(to: back) } label: {
                            HStack(spacing: 6) {
                                Image(systemName: "chevron.left").font(.system(size: 11, weight: .bold))
                                Text("Back")
                            }
                        }
                        .buttonStyle(ThemeButtonStyle(kind: .quiet))
                    }
                    Spacer()
                    if step == .play { startButton }
                    if let next, let following = Step(rawValue: step.rawValue + 1) {
                        Button { go(to: following) } label: {
                            HStack(spacing: 6) {
                                Text("Next: \(next.title)")
                                Image(systemName: "chevron.right").font(.system(size: 11, weight: .bold))
                            }
                        }
                        .buttonStyle(ThemeButtonStyle())
                        .disabled(!next.enabled)
                    }
                }
                .padding(.top, 14)
            }
            .padding(.horizontal, 30)
            .padding(.bottom, 22)
        }
        .frame(width: width)
        .allowsHitTesting(shown == step)
        .accessibilityHidden(shown != step)
    }

    // MARK: Step 1, Forged Alliance files

    @ViewBuilder private var gamePage: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let game = model.game {
                if game.isValid {
                    StatusRow(kind: .good, title: game.isAppCopy ? "Using FAF Native's own copy" : "Found",
                              detail: game.url.path, monospaced: true) {
                        if let size = model.gameSize {
                            Text(gigabytes(size)).font(.system(size: 13)).foregroundStyle(Theme.secondary)
                        }
                    }
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
            HStack(spacing: 8) {
                Button("Choose Folder…") { model.chooseFolder() }
                    .buttonStyle(ThemeButtonStyle(kind: gameReady ? .secondary : .primary))
                if let game = model.game {
                    Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([game.url]) }
                        .buttonStyle(ThemeButtonStyle())
                }
                if !model.others.isEmpty {
                    Menu("Other Installations") {
                        ForEach(model.others) { other in
                            Button(other.url.path) { model.use(other.url) }
                        }
                    }
                    .menuStyle(.borderlessButton)
                    .font(.system(size: 13, weight: .medium))
                    .fixedSize()
                    .padding(.horizontal, 14)
                    .frame(height: 32)
                    .background(Theme.control, in: RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.white.opacity(0.10)))
                }
            }
            .disabled(model.isCopying)
        }
        copyBox
    }

    @ViewBuilder private var copyBox: some View {
        if let fraction = model.copyFraction {
            VStack(alignment: .leading, spacing: 8) {
                ProgressView(value: fraction).tint(Theme.accent)
                HStack {
                    Text(model.copyText).font(.system(size: 12.5)).foregroundStyle(Theme.secondary)
                    Spacer()
                    Button("Cancel") { model.cancelCopy() }.buttonStyle(ThemeButtonStyle(kind: .small))
                }
            }
            .padding(.vertical, 14)
            .padding(.horizontal, 16)
            .background(Theme.box, in: RoundedRectangle(cornerRadius: 10))
        } else if let game = model.game, game.isValid, !game.isAppCopy {
            HStack(spacing: 16) {
                VStack(alignment: .leading, spacing: 3) {
                    (Text("Keep a copy inside FAF Native ").fontWeight(.semibold)
                        + Text("· optional").foregroundColor(Theme.tertiary))
                        .font(.system(size: 13.5))
                    Text(model.gameSize.map { "Copies the \(gigabytes($0)) folder into FAF Native's own storage, so the original installation can go." }
                        ?? "Measuring the folder…")
                        .font(.system(size: 12.5))
                        .foregroundStyle(Theme.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                Button("Copy into FAF Native") { model.startCopy() }
                    .buttonStyle(ThemeButtonStyle())
                    .disabled(model.gameSize == nil)
            }
            .padding(.vertical, 14)
            .padding(.horizontal, 16)
            .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Color.white.opacity(0.18), style: StrokeStyle(lineWidth: 1, dash: [4, 3])))
        }
    }

    // MARK: Step 2, FAF client

    @ViewBuilder private var clientPage: some View {
        if !model.rosettaInstalled {
            StatusRow(kind: .warning, title: "Rosetta isn't installed",
                      detail: "The FAF client's Java runtime is made for Intel Macs; Rosetta runs it on Apple silicon.") {
                Button(model.rosettaInstalling ? "Installing…" : "Install Rosetta…") { model.installRosetta() }
                    .buttonStyle(ThemeButtonStyle(kind: .primary))
                    .disabled(model.rosettaInstalling)
            }
        }
        installRow
        connectionRow
        locationRows
        troubleshooting
    }

    @ViewBuilder private var installRow: some View {
        if let fraction = model.installFraction {
            VStack(alignment: .leading, spacing: 8) {
                ProgressView(value: fraction).tint(Theme.accent)
                HStack {
                    Text(model.installText).font(.system(size: 12.5)).foregroundStyle(Theme.secondary)
                    Spacer()
                    Button("Cancel") { model.cancelClientInstall() }.buttonStyle(ThemeButtonStyle(kind: .small))
                }
            }
            .padding(.vertical, 14)
            .padding(.horizontal, 16)
            .background(Theme.box, in: RoundedRectangle(cornerRadius: 10))
        } else if let installed = model.installedClient {
            StatusRow(kind: .good, title: "FAF client \(installed.clientVersion) is set up",
                      detail: "Java \(installed.javaVersion) and JavaFX \(installed.javafxVersion) for macOS, in \(Places.clientInstall.path)") {
                HStack(spacing: 8) {
                    if model.checkingClientUpdate { ProgressView().controlSize(.small) }
                    Button("Check for Updates") { model.checkForClientUpdate() }
                        .buttonStyle(ThemeButtonStyle())
                        .disabled(model.checkingClientUpdate || model.isInstallingClient || model.clientBusy != nil)
                }
            }
        } else {
            StatusRow(kind: .info,
                      title: model.client.hasData || model.clientPrefsExist
                          ? "Your FAF client was set up outside FAF Native" : "Let FAF Native set up the FAF client",
                      detail: (model.client.hasData || model.clientPrefsExist ? "FAF Native can also keep a copy of its own and update it. " : "")
                          + releaseSummary) {
                Button("Download and Install") { model.installClient() }
                    .buttonStyle(ThemeButtonStyle(kind: .primary))
                    .disabled(model.latestRelease == nil || model.clientBusy != nil)
            }
        }
    }

    private var connectionRow: some View {
        StatusRow(kind: connected ? .good : .info,
                  title: connected ? "Connected to FAF Native" : (model.clientRunning ? "Sign in, then quit the client" : "Not connected yet"),
                  detail: connected
                      ? "Games started in the FAF client open in FAF Native."
                      : "First time: start the client, sign in, then quit it. When it closes, FAF Native connects it.") {
            Button { model.startClient() } label: {
                Text(model.clientRunning ? "FAF Client Is Running" : "Start FAF Client").opacity(model.clientLaunching ? 0 : 1)
            }
            .overlay { if model.clientLaunching { ProgressView().controlSize(.small) } }
            .buttonStyle(ThemeButtonStyle(kind: connected || model.installedClient == nil ? .secondary : .primary))
            .disabled(model.installedClient == nil || !model.rosettaInstalled || model.clientRunning
                      || model.clientBusy != nil || model.isInstallingClient)
        }
    }

    /// When the client's Forged Alliance location differs from the folder chosen in step 1.
    @ViewBuilder private var locationRows: some View {
        if locationDiffers, let game = model.game, let clientPath = model.client.installationPath {
            VStack(alignment: .leading, spacing: 8) {
                StatusRow(kind: .warning, title: "The client looks for Forged Alliance somewhere else", detail: clientPath, monospaced: true)
                Text("Reapply below, or set the client's Forged Alliance location yourself to:")
                    .font(.system(size: 12.5)).foregroundStyle(Theme.secondary)
                CopyField(text: game.url.path)
            }
        }
    }

    private var troubleshooting: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("If something goes wrong").caption()
            HStack(spacing: 8) {
                Button("Quit FAF Client") { confirmStop = true }
                    .disabled(!model.clientRunning || model.clientBusy != nil)
                    .help(model.clientRunning ? "Quits the FAF client" : "The FAF client isn't running")
                Button("Reset Window…") { confirmReset = true }
                    .disabled(!model.clientPrefsExist || model.clientBusy != nil)
                    .help("Puts the FAF client's window back to its default size and place on the main screen")
                Button("Reapply…") { confirmApply = true }
                    .disabled(!model.clientPrefsExist || model.clientRunning || model.clientBusy != nil
                              || Places.translocated || !gameReady)
                    .help("Reconnects games started in the FAF client to FAF Native and backs up its settings")
                if let busy = model.clientBusy {
                    ProgressView().controlSize(.small)
                    Text(busy).font(.system(size: 12.5)).foregroundStyle(Theme.secondary)
                }
            }
            .buttonStyle(ThemeButtonStyle(kind: .small))
            Text("Reset Window puts the client's window back on the main screen. Reapply reconnects the client to FAF Native and backs up its settings.")
                .font(.system(size: 12))
                .foregroundStyle(Theme.tertiary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var releaseSummary: String {
        guard let release = model.latestRelease else { return model.releaseProblem ?? "Checking for the latest version…" }
        let megabytes = Int((Double(release.client.size + release.java.size) + 47e6) / 1e6)
        return "Version \(release.version), with Java \(release.javaVersion) and the macOS JavaFX libraries: about \(megabytes) MB "
            + "from GitHub, Adoptium and Maven Central, each checked against the checksum its source publishes."
    }

    // MARK: Step 3, Play

    @ViewBuilder private var playPage: some View {
        VStack(spacing: 0) {
            summaryLine(done: gameReady, title: "Forged Alliance files", status: gameStatus.text)
            Rectangle().fill(Color.white.opacity(0.07)).frame(height: 1).padding(.horizontal, 16)
            summaryLine(done: clientReady, title: model.installedClient.map { "FAF client \($0.clientVersion)" } ?? "FAF client",
                        status: clientReady ? "Connected to FAF Native" : clientStatus.text)
        }
        .background(Theme.box, in: RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Color.white.opacity(0.06)))
        InterfaceScaleSection()
            .padding(.vertical, 14)
            .padding(.horizontal, 16)
            .background(Theme.box, in: RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Color.white.opacity(0.06)))
    }

    /// The Play page's main action, in its footer so it stays in view.
    private var startButton: some View {
        Button { model.startClient() } label: {
            HStack(spacing: 8) {
                Image(systemName: "play.fill").font(.system(size: 11))
                Text(model.clientRunning ? "FAF Client Is Running" : "Start FAF Client")
            }
        }
        .buttonStyle(ThemeButtonStyle(kind: .primary))
        .disabled(model.installedClient == nil || !model.rosettaInstalled || model.clientRunning || model.clientBusy != nil)
    }

    private func summaryLine(done: Bool, title: String, status: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: done ? "checkmark.circle.fill" : "exclamationmark.circle.fill")
                .symbolRenderingMode(.palette)
                .foregroundStyle(Theme.panel, done ? Theme.green : Theme.orange)
                .font(.system(size: 16))
            Text(title).font(.system(size: 13.5, weight: .semibold))
            Spacer()
            Text(status).font(.system(size: 12.5)).foregroundStyle(Theme.secondary)
        }
        .padding(.vertical, 12)
        .padding(.horizontal, 16)
    }

    private func gigabytes(_ bytes: Int64) -> String { String(format: "%.1f GB", Double(bytes) / 1e9) }
}

/// The toolbar's details button (in the full setup and the simple window): version, last launch,
/// the logs folder.
struct AppDetailsButton: View {
    @EnvironmentObject private var model: SetupModel
    @State private var showAppDetails = false

    var body: some View {
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

/// One of the three step cards: its badge (a tick when done), name and state. The card in view is
/// outlined in the accent colour.
private struct StepCard: View {
    enum Badge { case done, play, number }
    let number: Int
    let title: String
    let status: String
    let statusColor: Color
    let badge: Badge
    let selected: Bool
    /// Waiting for an earlier step: the name is dimmed.
    let waiting: Bool
    let action: () -> Void
    @State private var hovering = false

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: 12)
        Button(action: action) {
            VStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 8) {
                    badgeView
                    Text("Step \(number)").caption()
                }
                Spacer(minLength: 4)
                Text(title)
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(waiting ? Theme.dimmed : Theme.text)
                Spacer(minLength: 2)
                Text(status)
                    .font(.system(size: 12.5))
                    .foregroundStyle(statusColor)
                    .lineLimit(1)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 13)
            .frame(maxWidth: .infinity, minHeight: 92, maxHeight: 92, alignment: .leading)
            .background(selected ? Theme.selectedFill : Theme.panel, in: shape)
            .overlay(shape.strokeBorder(selected ? Theme.accent : Theme.line))
            .background(RoundedRectangle(cornerRadius: 15).fill(selected ? Theme.selectedRing : .clear).padding(-3))
            .contentShape(shape)
            .brightness(hovering && !selected ? 0.05 : 0)
            .animation(.easeOut(duration: 0.25), value: selected)
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
        .accessibilityLabel("Step \(number), \(title)")
        .accessibilityValue(status)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    @ViewBuilder private var badgeView: some View {
        ZStack {
            Circle().fill(badgeFill)
            switch badge {
            case .done:
                Image(systemName: "checkmark").font(.system(size: 11, weight: .heavy)).foregroundStyle(Theme.green)
            case .play:
                Image(systemName: "play.fill").font(.system(size: 9)).foregroundStyle(Theme.accentText)
            case .number:
                Text("\(number)").font(.system(size: 12, weight: .bold)).foregroundStyle(selected ? Theme.accentText : Theme.secondary)
            }
        }
        .frame(width: 22, height: 22)
    }

    private var badgeFill: Color {
        switch badge {
        case .done: return Theme.greenBadge
        case .play: return Theme.accentSoft
        case .number: return selected ? Theme.accentSoft : Theme.badge
        }
    }
}
