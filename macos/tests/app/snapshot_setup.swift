// Draws FAF Native's setup window (macos/app/setup/SetupView.swift) offscreen into PNGs, light and
// dark, from the model's real state on this Mac: a way to look at the window without opening it on
// screen. macos/tests/app/snapshot_setup.sh builds and runs it from inside a copy of the app, so the
// paths it shows (the launcher line) are the app's. With FAF_NATIVE_TEST_HOME set the app sees that
// folder as the home; SNAPSHOT_COPY=1 also copies the game files into it first, SNAPSHOT_APPLY=1
// presses "Reapply...", SNAPSHOT_INSTALL_CLIENT=1 downloads and installs the FAF client into it.
import AppKit
import SwiftUI

@main
struct SnapshotSetup {
    @MainActor static func main() throws {
        NSApplication.shared.setActivationPolicy(.prohibited)
        let base = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "setup"
        let size = NSRect(x: 0, y: 0, width: 640, height: 1150)
        let model = SetupModel()
        // The folder size is measured and the latest client release fetched off the main thread.
        let ready = Date().addingTimeInterval(20)
        repeat { RunLoop.main.run(until: Date().addingTimeInterval(0.5)) }
        while (model.latestRelease == nil && model.releaseProblem == nil) && Date() < ready
        RunLoop.main.run(until: Date().addingTimeInterval(1))
        let env = ProcessInfo.processInfo.environment
        if env["SNAPSHOT_COPY"] == "1" {
            model.startCopy()
            let deadline = Date().addingTimeInterval(60)
            while model.isCopying && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.2)) }
            print("copy finished: \(model.game?.url.path ?? "no folder"), alert: \(model.alert ?? "none")")
        }
        if env["SNAPSHOT_INSTALL_CLIENT"] == "1" && model.installedClient == nil {
            model.installClient()
            let deadline = Date().addingTimeInterval(900)
            var lastText = ""
            while model.isInstallingClient && Date() < deadline {
                RunLoop.main.run(until: Date().addingTimeInterval(0.5))
                if model.installText != lastText && !model.installText.hasPrefix("Downloading") { lastText = model.installText; print("  " + lastText) }
            }
            print("client install: \(model.installedClient.map { "\($0.clientVersion), Java \($0.javaVersion), JavaFX \($0.javafxVersion)" } ?? "none"), alert: \(model.alert ?? "none")")
            model.alert = nil
        }
        if env["SNAPSHOT_APPLY"] == "1" {
            model.applyClientSettings()
            print("apply: \(model.alert ?? "no message")")
            model.alert = nil
        }
        for (name, look) in [("light", NSAppearance.Name.aqua), ("dark", NSAppearance.Name.darkAqua)] {
            let view = NSHostingView(rootView: SetupView().environmentObject(model).frame(width: size.width, height: size.height))
            view.frame = size
            let window = NSWindow(contentRect: size, styleMask: [.titled], backing: .buffered, defer: false)
            window.appearance = NSAppearance(named: look)
            window.contentView = view
            view.layoutSubtreeIfNeeded()
            RunLoop.main.run(until: Date().addingTimeInterval(1))
            guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { continue }
            view.cacheDisplay(in: view.bounds, to: rep)
            try rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: "\(base)-\(name).png"))
            print("\(base)-\(name).png")
        }
    }
}
