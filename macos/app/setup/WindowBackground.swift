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
