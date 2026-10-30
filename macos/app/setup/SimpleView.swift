// FAF Native's simple window (the "Simple mode B: hangar" board of the FAF Native Setup Redesign
// mock-up): what the app opens with once everything is set up and the full setup has been seen
// ready once (SetupModel.simpleMode). The disk image's artwork behind the icon, the client's state
// and one main action: Start FAF Client, or Show FAF Client while it runs. Setup & Settings opens
// the full setup. The window keeps one size (WindowMode, WindowBackground.swift) and nothing in it
// scrolls.
import AppKit
import SwiftUI

struct SimpleView: View {
    /// The content under the toolbar; the window is 52 points taller and the artwork fills all of it.
    static let contentSize = CGSize(width: 640, height: 388)
    static let windowSize = CGSize(width: 640, height: 440)
    private static let icon = Bundle.main.url(forResource: "FAFNative", withExtension: "icns")
        .flatMap(NSImage.init(contentsOf:)) ?? NSImage(named: NSImage.applicationIconName) ?? NSImage()

    @EnvironmentObject private var model: SetupModel
    @State private var confirmStop = false

    var body: some View {
        VStack(spacing: 0) {
            main.frame(maxWidth: .infinity, maxHeight: .infinity)
            footer
        }
        .frame(width: Self.contentSize.width, height: Self.contentSize.height)
        .background(alignment: .bottom) { Backdrop().ignoresSafeArea() }
        .foregroundStyle(Theme.text)
        .preferredColorScheme(.dark)
        .toolbar {
            ToolbarItem(placement: .primaryAction) { AppDetailsButton() }
        }
        .toolbarBackground(.hidden, for: .windowToolbar)
        .onReceive(Timer.publish(every: 3, on: .main, in: .common).autoconnect()) { _ in model.pollClient() }
        .confirmationDialog("Quit the FAF client?", isPresented: $confirmStop) {
            Button("Quit FAF Client", role: .destructive) { model.stopClient() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("A game in progress ends too.")
        }
        .alert("FAF Native", isPresented: Binding(get: { model.alert != nil }, set: { if !$0 { model.alert = nil } })) {
            Button("OK") { model.alert = nil }
        } message: {
            Text(model.alert ?? "")
        }
    }

    private var main: some View {
        VStack(spacing: 0) {
            Image(nsImage: Self.icon)
                .resizable()
                .frame(width: 88, height: 88)
                .shadow(color: .black.opacity(0.55), radius: 13, y: 12)
                .accessibilityHidden(true)
            Text("FAF Native")
                .font(.system(size: 26, weight: .bold))
                .padding(.top, 12)
            Text("Forged Alliance Forever, running natively on Apple silicon")
                .font(.system(size: 13))
                .foregroundStyle(Theme.hangarText)
                .padding(.top, 4)
            statusPill
                .padding(.top, 14)
            mainButton
                .padding(.top, 22)
            // Kept in the layout while the client is stopped, so nothing moves when it starts.
            Button("Quit FAF Client") { confirmStop = true }
                .buttonStyle(ThemeButtonStyle(kind: .quiet))
                .disabled(!model.clientRunning || model.clientBusy != nil)
                .opacity(model.clientRunning ? 1 : 0)
                .accessibilityHidden(!model.clientRunning)
                .padding(.top, 6)
        }
        .padding(.top, 12)
    }

    private var status: (text: String, live: Bool) {
        if let busy = model.clientBusy { return (busy, false) }
        if model.clientLaunching { return ("Starting the FAF client…", true) }
        if model.clientRunning { return ("FAF client is running", true) }
        return ("Ready to play", false)
    }

    private var statusPill: some View {
        HStack(spacing: 8) {
            StatusDot(live: status.live)
            Text(status.text)
        }
        .font(.system(size: 12.5))
        .foregroundStyle(Theme.hangarPillText)
        .padding(.horizontal, 12)
        .frame(height: 28)
        .background(Theme.hangarPill, in: Capsule())
        .overlay(Capsule().strokeBorder(Color.white.opacity(0.12)))
    }

    private var mainButton: some View {
        Button {
            if model.clientRunning { model.showClient() } else { model.startClient() }
        } label: {
            HStack(spacing: 10) {
                Image(systemName: model.clientRunning ? "macwindow" : "play.fill")
                    .font(.system(size: model.clientRunning ? 16 : 14, weight: .semibold))
                Text(model.clientRunning ? "Show FAF Client" : "Start FAF Client")
            }
        }
        .buttonStyle(LaunchButtonStyle())
        .disabled(model.clientBusy != nil)
    }

    private var footer: some View {
        HStack(spacing: 14) {
            check("Game files")
            check(model.installedClient.map { "FAF client \($0.clientVersion)" } ?? "FAF client")
            Spacer()
            Button { model.showFullSetup() } label: {
                HStack(spacing: 7) {
                    Image(systemName: "slider.horizontal.3").font(.system(size: 12, weight: .medium))
                    Text("Setup & Settings")
                }
            }
            .buttonStyle(BarButtonStyle())
        }
        .padding(.leading, 20)
        .padding(.trailing, 14)
        .frame(height: 52)
        .background(Theme.hangarBar)
        .overlay(alignment: .top) { Rectangle().fill(Theme.line).frame(height: 1) }
    }

    private func check(_ text: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: "checkmark").font(.system(size: 10, weight: .heavy)).foregroundStyle(Theme.green)
            Text(text)
        }
        .font(.system(size: 12))
        .foregroundStyle(Theme.hangarText)
    }
}

/// The disk image's artwork (bundled by make_app.sh) over the whole window, under the toolbar too.
private struct Backdrop: View {
    private static let art = Bundle.main.url(forResource: "background-art", withExtension: "jpg")
        .flatMap(NSImage.init(contentsOf:))

    var body: some View {
        ZStack {
            Theme.hangar
            if let art = Self.art {
                Image(nsImage: art).resizable().aspectRatio(contentMode: .fill)
            }
        }
        .frame(width: SimpleView.windowSize.width, height: SimpleView.windowSize.height)
        .clipped()
        .accessibilityHidden(true)
    }
}

/// Green, and pulsing while the client runs.
private struct StatusDot: View {
    let live: Bool
    @State private var pulse = false

    var body: some View {
        Circle()
            .fill(Theme.green)
            .frame(width: 8, height: 8)
            .overlay {
                if live {
                    Circle()
                        .fill(Theme.green.opacity(0.55))
                        .scaleEffect(pulse ? 2.75 : 1)
                        .opacity(pulse ? 0 : 1)
                        .onAppear {
                            withAnimation(.easeOut(duration: 1.8).repeatForever(autoreverses: false)) { pulse = true }
                        }
                        .onDisappear { pulse = false }
                }
            }
    }
}

/// The main action: large, in the accent colour, glowing over the artwork's lit floor.
private struct LaunchButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View { Styled(configuration: configuration) }

    private struct Styled: View {
        let configuration: Configuration
        @Environment(\.isEnabled) private var isEnabled
        @State private var hovering = false

        var body: some View {
            let shape = RoundedRectangle(cornerRadius: 13)
            configuration.label
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(Color.white)
                .frame(width: 300, height: 52)
                .background(Theme.accent, in: shape)
                .overlay(shape.strokeBorder(Theme.accentEdge))
                .shadow(color: Theme.accent.opacity(0.45), radius: 15, y: 10)
                .contentShape(shape)
                .brightness(isEnabled && hovering && !configuration.isPressed ? 0.07 : 0)
                .opacity(isEnabled ? (configuration.isPressed ? 0.8 : 1) : 0.4)
                .onHover { hovering = $0 }
        }
    }
}

/// Setup & Settings: a translucent button on the footer bar.
private struct BarButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View { Styled(configuration: configuration) }

    private struct Styled: View {
        let configuration: Configuration
        @State private var hovering = false

        var body: some View {
            let shape = RoundedRectangle(cornerRadius: 8)
            configuration.label
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(Theme.text)
                .padding(.leading, 11)
                .padding(.trailing, 14)
                .frame(height: 32)
                .background(Color.white.opacity(hovering ? 0.13 : 0.08), in: shape)
                .overlay(shape.strokeBorder(Color.white.opacity(0.14)))
                .contentShape(shape)
                .opacity(configuration.isPressed ? 0.8 : 1)
                .onHover { hovering = $0 }
        }
    }
}
