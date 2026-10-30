// The setup window's look (the "FAF Native Setup Redesign" mock-up): a dark window, three step
// cards over one panel whose pages slide sideways, status boxes, and an accent blue for the step in
// view and the main action. SetupView.swift lays the window out with these.
import AppKit
import SwiftUI

enum Theme {
    static let accent = Color(hex: 0x1A6EF0)
    static let accentText = mix(0x1A6EF0, 0xFFFFFF, 0.4)
    static let accentSoft = mix(0x1D1D1F, 0x1A6EF0, 0.3)
    static let selectedFill = mix(0x1D1D1F, 0x1A6EF0, 0.16)
    static let selectedRing = mix(0x2A2A2D, 0x1A6EF0, 0.32)

    static let window = Color(hex: 0x2A2A2D)
    static let windowNS = NSColor(srgbRed: 0x2A / 255.0, green: 0x2A / 255.0, blue: 0x2D / 255.0, alpha: 1)
    static let panel = Color(hex: 0x1D1D1F)
    static let box = Color(hex: 0x262629)
    static let control = Color(hex: 0x3A3A3D)
    static let smallControl = Color(hex: 0x333336)
    static let badge = Color(hex: 0x2E2E31)
    static let line = Color.white.opacity(0.08)

    static let text = Color(hex: 0xF5F5F7)
    static let dimmed = Color(hex: 0xC7C7CC)
    static let secondary = Color(hex: 0xA1A1A6)
    static let tertiary = Color(hex: 0x8E8E93)

    static let green = Color(hex: 0x30D158)
    static let greenBadge = Color(hex: 0x1F3D29)
    static let red = Color(hex: 0xFF453A)
    static let redText = Color(hex: 0xFF6961)
    static let redBox = Color(hex: 0x2A1F1F)
    static let orange = Color(hex: 0xFF9F0A)
    static let orangeBox = mix(0x1D1D1F, 0xFF9F0A, 0.1)

    // The simple window, over the disk image's artwork (SimpleView.swift).
    static let hangar = Color(hex: 0x0B1528)
    static let hangarText = Color(hex: 0xB4BFD2)
    static let hangarPillText = Color(hex: 0xC7CEDE)
    static let hangarPill = Color(.sRGB, red: 8 / 255.0, green: 14 / 255.0, blue: 26 / 255.0, opacity: 0.72)
    static let hangarBar = Color(.sRGB, red: 6 / 255.0, green: 10 / 255.0, blue: 20 / 255.0, opacity: 0.8)
    static let accentEdge = mix(0x1A6EF0, 0xFFFFFF, 0.24)

    /// How pages and the step marker slide.
    static let slide = Animation.timingCurve(0.22, 0.8, 0.24, 1, duration: 0.42)

    private static func mix(_ from: UInt32, _ to: UInt32, _ amount: Double) -> Color {
        func channel(_ value: UInt32, _ shift: UInt32) -> Double { Double((value >> shift) & 0xFF) / 255 }
        func blend(_ shift: UInt32) -> Double { channel(from, shift) + (channel(to, shift) - channel(from, shift)) * amount }
        return Color(.sRGB, red: blend(16), green: blend(8), blue: blend(0), opacity: 1)
    }
}

extension Color {
    init(hex: UInt32) {
        self.init(.sRGB, red: Double((hex >> 16) & 0xFF) / 255, green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255, opacity: 1)
    }
}

extension Text {
    /// The small upper-case label over a step ("STEP 2", "IF SOMETHING GOES WRONG").
    func caption() -> some View {
        font(.system(size: 11, weight: .semibold)).tracking(0.66).textCase(.uppercase).foregroundStyle(Theme.tertiary)
    }
}

/// The window's buttons: the accent one for a step's main action, grey ones for the rest.
struct ThemeButtonStyle: ButtonStyle {
    enum Kind { case primary, secondary, small, quiet }
    var kind: Kind = .secondary

    func makeBody(configuration: Configuration) -> some View { Styled(configuration: configuration, kind: kind) }

    private struct Styled: View {
        let configuration: Configuration
        let kind: Kind
        @Environment(\.isEnabled) private var isEnabled
        @State private var hovering = false

        var body: some View {
            let shape = RoundedRectangle(cornerRadius: radius)
            configuration.label
                .font(font)
                .foregroundStyle(kind == .quiet ? Color(hex: 0xD1D1D6) : (accented ? Color.white : Theme.text))
                .padding(.horizontal, padding)
                .frame(height: height)
                .background(fill, in: shape)
                .overlay(shape.strokeBorder(accented ? Theme.accent : Color.white.opacity(0.10)))
                .contentShape(shape)
                .brightness(isEnabled && hovering && !configuration.isPressed ? 0.07 : 0)
                .opacity(isEnabled ? (configuration.isPressed ? 0.8 : 1) : 0.4)
                .onHover { hovering = $0 }
        }

        private var accented: Bool { kind == .primary }
        private var fill: Color {
            switch kind {
            case .primary: return Theme.accent
            case .secondary: return Theme.control
            case .small: return Theme.smallControl
            case .quiet: return .clear
            }
        }
        private var height: CGFloat {
            switch kind {
            case .primary: return 36
            case .secondary, .quiet: return 32
            case .small: return 28
            }
        }
        private var radius: CGFloat {
            switch kind {
            case .primary: return 9
            case .secondary, .quiet: return 8
            case .small: return 7
            }
        }
        private var padding: CGFloat {
            switch kind {
            case .primary: return 18
            case .secondary, .quiet: return 14
            case .small: return 12
            }
        }
        private var font: Font {
            switch kind {
            case .primary: return .system(size: 13.5, weight: .semibold)
            case .secondary, .quiet: return .system(size: 13, weight: .medium)
            case .small: return .system(size: 12.5, weight: .medium)
            }
        }
    }
}

/// A status box: a coloured symbol, a short statement, optional detail under it, and optional
/// controls at its end.
struct StatusRow<Accessory: View>: View {
    enum Kind { case good, warning, problem, info }
    let kind: Kind
    let title: String
    var detail: String?
    var monospaced = false
    let accessory: Accessory

    init(kind: Kind, title: String, detail: String? = nil, monospaced: Bool = false, @ViewBuilder accessory: () -> Accessory) {
        self.kind = kind
        self.title = title
        self.detail = detail
        self.monospaced = monospaced
        self.accessory = accessory()
    }

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: 10)
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            symbol.font(.system(size: 18))
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.system(size: 14, weight: .semibold))
                if let detail {
                    Text(detail)
                        .font(monospaced ? .system(size: 12, design: .monospaced) : .system(size: 12.5))
                        .lineSpacing(2)
                        .foregroundStyle(kind == .problem ? Theme.dimmed : Theme.secondary)
                        .textSelection(.enabled)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            accessory
        }
        .padding(.vertical, 14)
        .padding(.horizontal, 16)
        .background(fill, in: shape)
        .overlay(shape.strokeBorder(border))
    }

    @ViewBuilder private var symbol: some View {
        switch kind {
        case .good:
            Image(systemName: "checkmark.circle.fill").symbolRenderingMode(.palette).foregroundStyle(Theme.panel, Theme.green)
        case .warning:
            Image(systemName: "exclamationmark.triangle.fill").symbolRenderingMode(.palette).foregroundStyle(Theme.panel, Theme.orange)
        case .problem:
            Image(systemName: "xmark.octagon.fill").symbolRenderingMode(.palette).foregroundStyle(Theme.panel, Theme.red)
        case .info:
            Image(systemName: "info.circle.fill").symbolRenderingMode(.palette).foregroundStyle(Color.white, Theme.accent)
        }
    }

    private var fill: Color {
        switch kind {
        case .good, .info: return Theme.box
        case .warning: return Theme.orangeBox
        case .problem: return Theme.redBox
        }
    }

    private var border: Color {
        switch kind {
        case .good, .info: return Color.white.opacity(0.06)
        case .warning: return Theme.orange.opacity(0.35)
        case .problem: return Theme.red.opacity(0.35)
        }
    }
}

extension StatusRow where Accessory == EmptyView {
    init(kind: Kind, title: String, detail: String? = nil, monospaced: Bool = false) {
        self.init(kind: kind, title: title, detail: detail, monospaced: monospaced) { EmptyView() }
    }
}

/// Text to paste somewhere else, with a Copy button.
struct CopyField: View {
    @EnvironmentObject private var model: SetupModel
    let text: String

    var body: some View {
        HStack(spacing: 12) {
            Text(text)
                .font(.system(size: 12, design: .monospaced))
                .foregroundStyle(Theme.secondary)
                .textSelection(.enabled)
                .lineLimit(3)
                .frame(maxWidth: .infinity, alignment: .leading)
            Button("Copy") { model.copyToClipboard(text) }
                .buttonStyle(ThemeButtonStyle(kind: .small))
        }
        .padding(.vertical, 10)
        .padding(.horizontal, 14)
        .background(Theme.box, in: RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Color.white.opacity(0.06)))
    }
}
