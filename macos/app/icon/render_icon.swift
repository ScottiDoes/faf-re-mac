// Draw the FAF Native icon at 1024 pixels. The packaging script uses the generated .icns.
import AppKit
import CoreGraphics

let size = 1024
guard CommandLine.arguments.count == 2,
      let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                                    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                    isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0,
                                    bitsPerPixel: 0),
      let graphics = NSGraphicsContext(bitmapImageRep: bitmap) else {
    fatalError("Usage: swift render_icon.swift output.png")
}

func color(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: CGColorSpaceCreateDeviceRGB(), components: [red / 255, green / 255, blue / 255, alpha])!
}

func gradient(_ colors: [CGColor]) -> CGGradient {
    CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors as CFArray, locations: nil)!
}

func polygon(_ points: [CGPoint]) -> CGPath {
    let path = CGMutablePath()
    path.move(to: points[0])
    for point in points.dropFirst() { path.addLine(to: point) }
    path.closeSubpath()
    return path
}

NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = graphics
let context = graphics.cgContext
context.setAllowsAntialiasing(true)
context.setShouldAntialias(true)

// A rounded macOS tile with an opaque, dark blue face.
let tile = CGPath(roundedRect: CGRect(x: 60, y: 60, width: 904, height: 904),
                  cornerWidth: 205, cornerHeight: 205, transform: nil)
context.saveGState()
context.setShadow(offset: CGSize(width: 0, height: -20), blur: 30, color: color(0, 0, 0, 0.35))
context.addPath(tile)
context.setFillColor(color(10, 22, 40))
context.fillPath()
context.restoreGState()

context.saveGState()
context.addPath(tile)
context.clip()
context.drawLinearGradient(gradient([color(32, 65, 96), color(12, 26, 48), color(7, 17, 34)]),
                           start: CGPoint(x: 170, y: 940), end: CGPoint(x: 840, y: 65), options: [])
context.drawRadialGradient(gradient([color(46, 209, 230, 0.20), color(46, 209, 230, 0)]),
                           startCenter: CGPoint(x: 518, y: 570), startRadius: 0,
                           endCenter: CGPoint(x: 518, y: 570), endRadius: 410, options: [])
context.restoreGState()

context.addPath(tile)
context.setStrokeColor(color(156, 221, 235, 0.28))
context.setLineWidth(10)
context.strokePath()

// The six-sided frame suggests a strategy-game command marker.
let shield = polygon([
    CGPoint(x: 512, y: 842), CGPoint(x: 786, y: 684), CGPoint(x: 786, y: 366),
    CGPoint(x: 512, y: 208), CGPoint(x: 238, y: 366), CGPoint(x: 238, y: 684),
])
context.saveGState()
context.addPath(shield)
context.setLineWidth(25)
context.setLineJoin(.round)
context.setStrokeColor(color(48, 198, 222, 0.35))
context.setShadow(offset: .zero, blur: 23, color: color(29, 211, 239, 0.42))
context.strokePath()
context.restoreGState()
context.addPath(shield)
context.setLineWidth(12)
context.setLineJoin(.round)
context.setStrokeColor(color(113, 218, 237, 0.70))
context.strokePath()

// A custom beveled F remains recognizable at Dock and Finder sizes.
let letter = polygon([
    CGPoint(x: 346, y: 291), CGPoint(x: 459, y: 291), CGPoint(x: 459, y: 456),
    CGPoint(x: 635, y: 456), CGPoint(x: 660, y: 559), CGPoint(x: 459, y: 559),
    CGPoint(x: 459, y: 625), CGPoint(x: 684, y: 625), CGPoint(x: 710, y: 735),
    CGPoint(x: 346, y: 735),
])
context.saveGState()
context.setShadow(offset: CGSize(width: 0, height: -16), blur: 18, color: color(0, 0, 0, 0.55))
context.addPath(letter)
context.setFillColor(color(100, 226, 245))
context.fillPath()
context.restoreGState()
context.saveGState()
context.addPath(letter)
context.clip()
context.drawLinearGradient(gradient([color(232, 253, 255), color(110, 223, 241), color(40, 148, 189)]),
                           start: CGPoint(x: 370, y: 735), end: CGPoint(x: 700, y: 290), options: [])
context.restoreGState()

guard let png = bitmap.representation(using: .png, properties: [:]) else { fatalError("Could not render PNG") }
try png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .atomic)
NSGraphicsContext.restoreGraphicsState()
