// The setup window's solid dark ground (Theme.window), also under its transparent title bar, so the
// title and the details button sit on the same colour as the steps.
import AppKit
import SwiftUI

struct SetupWindowBackground: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView { ThemedBackgroundView() }

    func updateNSView(_ view: NSView, context: Context) {}
}

private final class ThemedBackgroundView: NSView {
    override var isOpaque: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        Theme.windowNS.setFill()
        dirtyRect.fill()
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.backgroundColor = Theme.windowNS
        window?.titlebarAppearsTransparent = true
        window?.appearance = NSAppearance(named: .darkAqua)
    }
}

/// What the window's mode changes in AppKit (AppWindow, FAFNativeApp.swift). The simple window has
/// one size (SwiftUI's contentSize resizability already pins it to SimpleView's frame), no title,
/// and no resizing, zoom or full screen; the full setup gets them back and opens at least at its
/// usual 880 x 780, under the same top edge: coming from the simple window, or launching in the
/// smaller frame SwiftUI restores after one.
struct WindowMode: NSViewRepresentable {
    let simple: Bool

    func makeNSView(context: Context) -> WindowModeView { WindowModeView() }

    func updateNSView(_ view: WindowModeView, context: Context) { view.simple = simple }
}

final class WindowModeView: NSView {
    private static let fullSize = NSSize(width: 880, height: 780)

    var simple: Bool? {
        didSet { if simple != oldValue { apply(animated: oldValue != nil) } }
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        apply(animated: false)
    }

    private func apply(animated: Bool) {
        guard let window, let simple else { return }
        window.backgroundColor = Theme.windowNS
        window.titlebarAppearsTransparent = true
        window.appearance = NSAppearance(named: .darkAqua)
        window.titleVisibility = simple ? .hidden : .visible
        if simple {
            window.styleMask.remove(.resizable)
            return
        }
        window.styleMask.insert(.resizable)
        // After SwiftUI has swapped the content and its size limits, and restored the last frame.
        DispatchQueue.main.async {
            let current = window.frame
            var size = NSSize(width: max(current.width, Self.fullSize.width), height: max(current.height, Self.fullSize.height))
            guard size != current.size else { return }
            let visible = window.screen?.visibleFrame
            if let visible {
                size = NSSize(width: min(size.width, visible.width), height: min(size.height, visible.height))
            }
            var frame = NSRect(x: current.minX, y: current.maxY - size.height, width: size.width, height: size.height)
            if let visible {
                frame.origin.x = min(max(frame.minX, visible.minX), visible.maxX - frame.width)
                frame.origin.y = max(frame.minY, visible.minY)
            }
            window.setFrame(frame, display: true, animate: animated)
        }
    }
}
