// FAF Native's setup and status app: one window (SetupView.swift) over the model in Setup.swift.
import AppKit
import SwiftUI

@main
struct FAFNativeApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = SetupModel()

    var body: some Scene {
        Window("FAF Native", id: "setup") {
            SetupView()
                .environmentObject(model)
                .frame(minWidth: 760, idealWidth: 880, minHeight: 640, idealHeight: 780)
        }
        .defaultSize(width: 880, height: 780)
        .windowResizability(.contentMinSize)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
