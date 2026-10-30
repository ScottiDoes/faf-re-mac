// FAF Native's setup and status app: one window over the model in Setup.swift, showing the full
// setup (SetupView.swift) or, once everything is set up, the simple window (SimpleView.swift).
import AppKit
import SwiftUI

@main
struct FAFNativeApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = SetupModel()

    var body: some Scene {
        Window("FAF Native", id: "setup") {
            AppWindow()
                .environmentObject(model)
        }
        .defaultSize(width: 880, height: 780)
        .windowResizability(.contentSize)
    }
}

/// The simple window when the model chose it at launch (everything ready, and the full setup seen
/// ready once before), else the full setup. Setup & Settings, or something that stops being ready,
/// switches to the full setup until the next launch.
private struct AppWindow: View {
    @EnvironmentObject private var model: SetupModel

    var body: some View {
        Group {
            if model.simpleMode {
                SimpleView()
            } else {
                SetupView()
                    .frame(minWidth: 760, idealWidth: 880, maxWidth: .infinity, minHeight: 640, idealHeight: 780, maxHeight: .infinity)
            }
        }
        .background { WindowMode(simple: model.simpleMode) }
        .onChange(of: model.isReady, initial: true) { _, ready in
            if ready && !model.simpleMode { model.noteSetupSeen() }
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
