// The game's UI Scale (FAF's Options > Interface > UI Scale), chosen in the setup window before
// the first game, with a preview of the size the interface will have on this Mac's screen. The
// game takes the choice at its next start from faf-native-settings.txt beside its preferences
// file, then deletes that file (src/moho/app/CScApp.cpp, ApplySetupWindowSettings).
import AppKit
import SwiftUI

/// The game's preferences folder: the UI Scale saved there, and the request for a new one.
enum GamePrefs {
    static var folder: URL {
        Places.home.appendingPathComponent("Library/Application Support/Gas Powered Games/Supreme Commander Forged Alliance",
                                           isDirectory: true)
    }
    /// The file the game uses: mac.prefs when it exists (faf-launch then passes /prefs mac.prefs), else Game.prefs.
    static var file: URL {
        let mac = folder.appendingPathComponent("mac.prefs")
        return FileManager.default.fileExists(atPath: mac.path) ? mac : folder.appendingPathComponent("Game.prefs")
    }
    static var request: URL { folder.appendingPathComponent("faf-native-settings.txt") }
    /// FAF's UI Scale steps (lua/options/options.lua).
    static let uiScales: [Double] = [0.8, 1.0, 1.25, 1.5, 1.75, 2.0]

    /// The UI Scale the game will use: a request it hasn't taken yet, else the one it saved.
    static func uiScale() -> (scale: Double, pending: Bool) {
        if let text = try? String(contentsOf: request, encoding: .utf8),
           let line = text.split(separator: "\n").first(where: { $0.hasPrefix("ui_scale") }),
           let value = line.split(separator: "=").last.flatMap({ Double($0.trimmingCharacters(in: .whitespaces)) }) {
            return (value, true)
        }
        return (savedUIScale() ?? 1.0, false)
    }

    /// The current profile's UI Scale; before FAF's lobby has made a profile, the default an
    /// earlier request left for it. Nil when the game hasn't saved one (a new install: 100%).
    static func savedUIScale() -> Double? {
        guard let text = try? String(contentsOf: file, encoding: .utf8), let prefs = LuaTable.parse(text) else { return nil }
        if let current = prefs["profile"]?["current"]?.number,
           let scale = prefs["profile"]?["profiles"]?[Int(current)]?["options"]?["ui_scale"]?.number {
            return scale
        }
        return prefs["options_overrides"]?["ui_scale"]?["default"]?.number
    }

    /// Asks the game for this UI Scale at its next start, or withdraws the request when the game already has it.
    static func requestUIScale(_ scale: Double) throws {
        if abs(scale - (savedUIScale() ?? 1.0)) < 0.001 {
            if FileManager.default.fileExists(atPath: request.path) { try FileManager.default.removeItem(at: request) }
            return
        }
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try "# FAF Native's setup window: options for the game to take at its next start, after which it deletes this file.\nui_scale = \(scale)\n"
            .write(to: request, atomically: true, encoding: .utf8)
    }
}

/// A value read from the game's preferences file, which is Lua: `name = value` statements whose
/// values are numbers, strings, booleans and tables. Only reading is needed.
indirect enum LuaTable {
    enum Key: Hashable { case name(String), index(Int) }
    case number(Double), string(String), bool(Bool), table([Key: LuaTable])

    subscript(name: String) -> LuaTable? {
        if case .table(let entries) = self { return entries[.name(name)] }
        return nil
    }
    subscript(index: Int) -> LuaTable? {
        if case .table(let entries) = self { return entries[.index(index)] }
        return nil
    }
    var number: Double? {
        if case .number(let value) = self { return value }
        return nil
    }

    /// The file's globals as one table; nil when it isn't the Lua the game writes.
    static func parse(_ text: String) -> LuaTable? {
        var parser = LuaReader(bytes: Array(text.utf8))
        return try? parser.file()
    }
}

private struct LuaReader {
    struct Malformed: Error {}
    let bytes: [UInt8]
    var at = 0

    mutating func file() throws -> LuaTable {
        var globals: [LuaTable.Key: LuaTable] = [:]
        skipSpace()
        while at < bytes.count {
            let name = try identifier()
            try expect("=")
            globals[.name(name)] = try value()
            skipSpace()
        }
        return .table(globals)
    }

    private func peek() -> UInt8? { at < bytes.count ? bytes[at] : nil }

    private mutating func expect(_ character: Character) throws {
        skipSpace()
        guard peek() == character.asciiValue else { throw Malformed() }
        at += 1
    }

    private mutating func skipSpace() {
        while let byte = peek() {
            if byte == 0x20 || byte == 0x09 || byte == 0x0A || byte == 0x0D {
                at += 1
            } else if byte == UInt8(ascii: "-"), at + 1 < bytes.count, bytes[at + 1] == UInt8(ascii: "-") {
                while let next = peek(), next != 0x0A { at += 1 }
            } else {
                return
            }
        }
    }

    private static func isNameByte(_ byte: UInt8, first: Bool) -> Bool {
        byte == UInt8(ascii: "_") || (byte >= 0x41 && byte <= 0x5A) || (byte >= 0x61 && byte <= 0x7A)
            || (!first && byte >= 0x30 && byte <= 0x39)
    }

    private mutating func identifier() throws -> String {
        skipSpace()
        let start = at
        while let byte = peek(), Self.isNameByte(byte, first: at == start) { at += 1 }
        guard at > start else { throw Malformed() }
        return String(decoding: bytes[start..<at], as: UTF8.self)
    }

    /// A value, nil for Lua's nil.
    private mutating func value() throws -> LuaTable? {
        skipSpace()
        guard let byte = peek() else { throw Malformed() }
        switch byte {
        case UInt8(ascii: "{"): return try table()
        case UInt8(ascii: "'"), UInt8(ascii: "\""): return .string(try string())
        case UInt8(ascii: "-"), UInt8(ascii: "."), 0x30...0x39: return .number(try number())
        default:
            switch try identifier() {
            case "true": return .bool(true)
            case "false": return .bool(false)
            case "nil": return nil
            default: throw Malformed()
            }
        }
    }

    private mutating func number() throws -> Double {
        let start = at
        while let byte = peek(), byte == UInt8(ascii: "-") || byte == UInt8(ascii: "+") || byte == UInt8(ascii: ".")
            || (byte >= 0x30 && byte <= 0x39) || byte == UInt8(ascii: "e") || byte == UInt8(ascii: "E") {
            at += 1
        }
        guard let value = Double(String(decoding: bytes[start..<at], as: UTF8.self)) else { throw Malformed() }
        return value
    }

    private mutating func string() throws -> String {
        let quote = bytes[at]
        at += 1
        var text: [UInt8] = []
        while let byte = peek(), byte != quote {
            at += 1
            guard byte == UInt8(ascii: "\\"), let escaped = peek() else { text.append(byte); continue }
            at += 1
            switch escaped {
            case UInt8(ascii: "n"): text.append(0x0A)
            case UInt8(ascii: "t"): text.append(0x09)
            case UInt8(ascii: "r"): text.append(0x0D)
            case 0x30...0x39:
                var code = Int(escaped - 0x30)
                for _ in 0..<2 {
                    guard let digit = peek(), digit >= 0x30 && digit <= 0x39 else { break }
                    code = code * 10 + Int(digit - 0x30)
                    at += 1
                }
                text.append(UInt8(truncatingIfNeeded: code))
            default: text.append(escaped)
            }
        }
        guard peek() == quote else { throw Malformed() }
        at += 1
        return String(decoding: text, as: UTF8.self)
    }

    private mutating func table() throws -> LuaTable {
        at += 1
        var entries: [LuaTable.Key: LuaTable] = [:]
        var nextIndex = 1
        while true {
            skipSpace()
            guard let byte = peek() else { throw Malformed() }
            if byte == UInt8(ascii: "}") {
                at += 1
                return .table(entries)
            }
            if byte == UInt8(ascii: "[") {
                at += 1
                let key = try value()
                try expect("]")
                try expect("=")
                let entry = try value()
                switch key {
                case .number(let number) where number == number.rounded(): entries[.index(Int(number))] = entry
                case .string(let name): entries[.name(name)] = entry
                default: break
                }
            } else if let name = keyName() {
                entries[.name(name)] = try value()
            } else {
                entries[.index(nextIndex)] = try value()
                nextIndex += 1
            }
            skipSpace()
            if peek() == UInt8(ascii: ",") || peek() == UInt8(ascii: ";") { at += 1 }
            else if peek() != UInt8(ascii: "}") { throw Malformed() }
        }
    }

    /// `name =` at this point (consumed), else nil with nothing consumed.
    private mutating func keyName() -> String? {
        let start = at
        guard let name = try? identifier() else { at = start; return nil }
        skipSpace()
        if peek() == UInt8(ascii: "="), at + 1 >= bytes.count || bytes[at + 1] != UInt8(ascii: "=") {
            at += 1
            return name
        }
        at = start
        return nil
    }
}

/// The size of one pixel of the game's interface on a screen, in points, at UI Scale 100%. The
/// game lays its interface out at the player's UI Scale times a correction for scaled desktops
/// (backing-store pixels per panel pixel, src/platform/posix/shell/Panel.cpp), and draws one
/// backing-store pixel per pixel, so 100% is one interface pixel per panel pixel.
struct ScreenScale {
    var pointsPerPixel: Double
    var size: CGSize

    static func of(_ screen: NSScreen?) -> ScreenScale {
        guard let screen else { return ScreenScale(pointsPerPixel: 1, size: CGSize(width: 1440, height: 900)) }
        let backing = Double(screen.frame.width * screen.backingScaleFactor)
        let id = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? CGDirectDisplayID
        let native = id.flatMap(nativePixelWidth) ?? backing
        let correction = max(1, backing / native)
        return ScreenScale(pointsPerPixel: correction / Double(screen.backingScaleFactor), size: screen.frame.size)
    }

    /// The panel's native width in pixels (Panel.cpp FindNative): the mode macOS flags as native,
    /// else the widest mode drawn at one pixel per point.
    private static func nativePixelWidth(_ display: CGDirectDisplayID) -> Double? {
        let options = [kCGDisplayShowDuplicateLowResolutionModes: kCFBooleanTrue] as CFDictionary
        guard let modes = CGDisplayCopyAllDisplayModes(display, options) as? [CGDisplayMode] else { return nil }
        let nativeFlag: UInt32 = 0x0200_0000
        if let native = modes.filter({ $0.ioFlags & nativeFlag != 0 }).map(\.pixelWidth).max() { return Double(native) }
        return modes.filter { $0.width == $0.pixelWidth }.map(\.pixelWidth).max().map(Double.init)
    }
}

/// Step 3's UI Scale slider and preview.
struct InterfaceScaleSection: View {
    @State private var step = 1.0
    @State private var pending = false
    @State private var problem: String?
    @State private var screen = ScreenScale.of(NSScreen.main)

    private var scale: Double { GamePrefs.uiScales[Int(step)] }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Interface size").fontWeight(.semibold)
                Spacer()
                Text("\(Int((scale * 100).rounded()))%").monospacedDigit()
            }
            Slider(value: $step, in: 0...Double(GamePrefs.uiScales.count - 1), step: 1) {
                EmptyView()
            } minimumValueLabel: {
                Text("80%").font(.caption)
            } maximumValueLabel: {
                Text("200%").font(.caption)
            }
            .accessibilityLabel("Interface size")
            .accessibilityValue("\(Int((scale * 100).rounded())) percent")
            HStack(alignment: .top, spacing: 12) {
                ScreenMiniature(screen: screen, scale: scale)
                InterfaceSample(pixel: screen.pointsPerPixel * scale)
            }
            if let problem {
                StatusRow(kind: .problem, title: problem)
            } else {
                Text(pending ? "The game takes this size the next time it starts." : "The size the game uses now.")
                    .font(.callout)
                Text("Left: roughly how much of this screen the game's controls cover. Right: part of the game's interface "
                    + "at actual size. In the game it's under Options, Interface, UI Scale.")
                    .font(.callout).foregroundStyle(.secondary)
            }
        }
        .onAppear(perform: load)
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { notification in
            if let window = notification.object as? NSWindow { screen = ScreenScale.of(window.screen) }
            load()
        }
        .onChange(of: step) { _, _ in save() }
    }

    private func load() {
        let current = GamePrefs.uiScale()
        let nearest = GamePrefs.uiScales.indices.min { abs(GamePrefs.uiScales[$0] - current.scale) < abs(GamePrefs.uiScales[$1] - current.scale) } ?? 1
        pending = current.pending
        if Double(nearest) != step { step = Double(nearest) }
    }

    private func save() {
        guard abs(scale - GamePrefs.uiScale().scale) >= 0.001 else { return } // what load() just read
        do {
            try GamePrefs.requestUIScale(scale)
            pending = FileManager.default.fileExists(atPath: GamePrefs.request.path)
            problem = nil
        } catch {
            problem = "Couldn't save the size for the game: \(error.localizedDescription)"
        }
    }
}

/// This screen in miniature with the game's controls drawn at the chosen UI Scale: FAF's
/// default layout, in interface pixels at 100% (approximate).
private struct ScreenMiniature: View {
    let screen: ScreenScale
    let scale: Double

    /// Rectangles in interface pixels: x, y from the top left (negative: from the right or bottom edge), width, height.
    private static let controls: [(CGFloat, CGFloat, CGFloat, CGFloat)] = [
        (0, 0, 320, 64),      // economy
        (-1, 0, 280, 150),    // score board
        (0, -1, 390, 170),    // orders and selected unit
        (390, -1, 640, 130),  // construction
    ]

    var body: some View {
        let width: CGFloat = 220
        let height = width * screen.size.height / max(screen.size.width, 1)
        let points = width / screen.size.width * CGFloat(screen.pointsPerPixel * scale)
        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 4).fill(Color(red: 0.16, green: 0.22, blue: 0.16))
            ForEach(Self.controls.indices, id: \.self) { index in
                let (x, y, w, h) = Self.controls[index]
                let size = CGSize(width: min(w * points, width), height: min(h * points, height))
                Rectangle()
                    .fill(Color(red: 0.78, green: 0.8, blue: 0.82))
                    .overlay(Rectangle().stroke(Color(white: 0.2), lineWidth: 0.5))
                    .frame(width: size.width, height: size.height)
                    .offset(x: x < 0 ? width - size.width : x * points, y: y < 0 ? height - size.height : 0)
            }
            Text("Map").font(.caption2).foregroundStyle(.white.opacity(0.5))
                .frame(width: width, height: height)
        }
        .frame(width: width, height: height)
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .accessibilityHidden(true)
    }
}

/// A piece of the game's interface at actual size: the economy readout and a row of build buttons.
private struct InterfaceSample: View {
    /// Points per interface pixel at the chosen UI Scale.
    let pixel: Double

    var body: some View {
        let px = CGFloat(pixel)
        VStack(alignment: .leading, spacing: 6 * px) {
            economyRow(name: "Mass", color: Color(red: 0.55, green: 0.85, blue: 0.3), rate: "+12.4", stored: "1,250 / 3,000", px: px)
            economyRow(name: "Energy", color: Color(red: 0.98, green: 0.8, blue: 0.25), rate: "+86", stored: "4,120 / 6,000", px: px)
            HStack(spacing: 4 * px) {
                ForEach(["T1", "T1", "T2", "T3"].indices, id: \.self) { index in
                    RoundedRectangle(cornerRadius: 3 * px)
                        .fill(Color(white: 0.22))
                        .overlay(RoundedRectangle(cornerRadius: 3 * px).stroke(Color(white: 0.5), lineWidth: max(0.5, px)))
                        .overlay(alignment: .topLeading) {
                            Text(["T1", "T1", "T2", "T3"][index]).font(.custom("Arial", size: 11 * px).bold())
                                .foregroundStyle(Color(white: 0.85)).padding(3 * px)
                        }
                        .frame(width: 48 * px, height: 48 * px)
                }
            }
        }
        .padding(8 * px)
        .fixedSize()
        .frame(maxWidth: .infinity, minHeight: 124, maxHeight: 124, alignment: .topLeading)
        .background(Color(red: 0.08, green: 0.09, blue: 0.1))
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .accessibilityHidden(true)
    }

    private func economyRow(name: String, color: Color, rate: String, stored: String, px: CGFloat) -> some View {
        HStack(spacing: 8 * px) {
            Circle().fill(color).frame(width: 12 * px, height: 12 * px)
            Text(name).frame(width: 56 * px, alignment: .leading)
            Text(rate).foregroundStyle(color).frame(width: 44 * px, alignment: .trailing)
            Text(stored)
        }
        .font(.custom("Arial", size: 13 * px))
        .foregroundStyle(Color(white: 0.85))
    }
}
