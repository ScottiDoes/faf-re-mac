// Draws the FAF Native disk image's window background, at 1x and 2x in one TIFF: art made with an
// image generator (background-art.jpg), darkened, then the title, a panel behind each icon, an
// arrow from the app to Applications, and the instructions. make_dmg.sh passes the window size and
// the icon positions it also gives layout.swift.
//
// Finder draws each icon's name in black in Light Mode and in white in Dark Mode, whatever the
// picture, so the panels behind the icons are a mid grey on which both read.
//
// Usage: swift background.swift <art> <output.tiff> <width> <height> <app x> <Applications x> <icons y>
import AppKit

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("background.swift: \(message)\n".utf8))
    exit(1)
}

let arguments = CommandLine.arguments
guard arguments.count == 8, let art = NSImage(contentsOfFile: arguments[1]),
      case let numbers = arguments[3...].compactMap({ Double($0) }), numbers.count == 5 else {
    fail("usage: swift background.swift <art> <output.tiff> <width> <height> <app x> <Applications x> <icons y>")
}
let (width, height, appX, applicationsX, iconsY) = (numbers[0], numbers[1], numbers[2], numbers[3], numbers[4])

func rgb(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(srgbRed: red / 255, green: green / 255, blue: blue / 255, alpha: alpha)
}

func text(_ string: String, size: CGFloat, weight: NSFont.Weight, alpha: CGFloat, top: CGFloat) {
    let shadow = NSShadow()
    shadow.shadowColor = NSColor(white: 0, alpha: 0.7)
    shadow.shadowBlurRadius = 6
    let line = NSAttributedString(string: string, attributes: [
        .font: NSFont.systemFont(ofSize: size, weight: weight),
        .foregroundColor: NSColor(white: 1, alpha: alpha),
        .shadow: shadow,
    ])
    line.draw(at: NSPoint(x: (width - line.size().width) / 2, y: top))
}

/// Draws in points from the top left; `scale` is pixels per point (shadows are set in pixels).
func draw(_ context: CGContext, scale: CGFloat) {
    // The art fills the window, darkened most at the top, behind the title.
    let fill = max(width / art.size.width, height / art.size.height)
    let artSize = CGSize(width: art.size.width * fill, height: art.size.height * fill)
    art.draw(in: CGRect(x: (width - artSize.width) / 2, y: (height - artSize.height) / 2,
                        width: artSize.width, height: artSize.height),
             from: .zero, operation: .copy, fraction: 1, respectFlipped: true,
             hints: [.interpolation: NSImageInterpolation.high.rawValue])
    let shade = CGGradient(colorsSpace: nil, colors: [rgb(0, 0, 0, 0.62), rgb(0, 0, 0, 0.38), rgb(0, 0, 0, 0.30)] as CFArray,
                           locations: [0, 0.45, 1])!
    context.drawLinearGradient(shade, start: .zero, end: CGPoint(x: 0, y: height), options: [])

    text("FAF Native", size: 26, weight: .bold, alpha: 1, top: 26)
    text("Forged Alliance Forever, running natively on Apple silicon", size: 13, weight: .regular, alpha: 0.72, top: 62)

    for x in [appX, applicationsX] {
        let panel = CGPath(roundedRect: CGRect(x: x - 100, y: iconsY - 84, width: 200, height: 184),
                           cornerWidth: 20, cornerHeight: 20, transform: nil)
        context.saveGState()
        context.setShadow(offset: CGSize(width: 0, height: -6 * scale), blur: 18 * scale, color: rgb(0, 0, 0, 0.5))
        context.addPath(panel)
        context.setFillColor(rgb(110, 117, 130, 0.94))
        context.fillPath()
        context.restoreGState()
        context.saveGState()
        context.addPath(panel)
        context.clip()
        let sheen = CGGradient(colorsSpace: nil, colors: [rgb(255, 255, 255, 0.12), rgb(255, 255, 255, 0)] as CFArray, locations: [0, 1])!
        context.drawLinearGradient(sheen, start: CGPoint(x: 0, y: iconsY - 84), end: CGPoint(x: 0, y: iconsY + 30), options: [])
        context.addPath(panel)
        context.setStrokeColor(rgb(255, 255, 255, 0.22))
        context.setLineWidth(2)
        context.strokePath()
        context.restoreGState()
    }

    // The arrow, glowing in the icon's cyan.
    let from = appX + 116, to = applicationsX - 116
    let arrow = CGMutablePath()
    arrow.move(to: CGPoint(x: from, y: iconsY))
    arrow.addLine(to: CGPoint(x: to - 12, y: iconsY))
    context.saveGState()
    context.setShadow(offset: .zero, blur: 12 * scale, color: rgb(79, 216, 240, 0.9))
    context.setStrokeColor(rgb(120, 228, 246))
    context.setFillColor(rgb(120, 228, 246))
    context.setLineWidth(5)
    context.setLineCap(.round)
    context.addPath(arrow)
    context.strokePath()
    context.move(to: CGPoint(x: to, y: iconsY))
    context.addLine(to: CGPoint(x: to - 19, y: iconsY - 12))
    context.addLine(to: CGPoint(x: to - 19, y: iconsY + 12))
    context.closePath()
    context.fillPath()
    context.restoreGState()

    text("Drag FAF Native onto the Applications folder to install it.", size: 15, weight: .semibold, alpha: 1, top: iconsY + 116)
    text("Then open FAF Native from Applications. It walks you through the rest of the setup.",
         size: 12.5, weight: .regular, alpha: 0.75, top: iconsY + 141)
}

let image = NSImage(size: NSSize(width: width, height: height))
for scale in [1.0, 2.0] {
    let pixelsWide = Int(width * scale), pixelsHigh = Int(height * scale)
    guard let context = CGContext(data: nil, width: pixelsWide, height: pixelsHigh, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { fail("cannot make a \(pixelsWide)x\(pixelsHigh) canvas") }
    context.interpolationQuality = .high
    context.translateBy(x: 0, y: CGFloat(pixelsHigh))
    context.scaleBy(x: scale, y: -scale)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: true)
    draw(context, scale: scale)
    NSGraphicsContext.restoreGraphicsState()
    guard let picture = context.makeImage() else { fail("cannot render at \(scale)x") }
    let representation = NSBitmapImageRep(cgImage: picture)
    representation.size = NSSize(width: width, height: height)   // so the 2x one is 144 dpi
    image.addRepresentation(representation)
}
guard let tiff = image.tiffRepresentation(using: .lzw, factor: 0) else { fail("cannot encode the TIFF") }
do { try tiff.write(to: URL(fileURLWithPath: arguments[2])) } catch { fail("cannot write \(arguments[2]): \(error)") }
