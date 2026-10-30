// Draw the FAF Native icon at 1024 pixels: FAFNative-art.png (made with an image generator) in
// the macOS app icon shape on Apple's icon grid, an 824-pixel rounded square in the middle of the
// canvas with a soft shadow below it. The art is drawn a little larger than the shape, so its outer
// edge is trimmed. make_icon.sh turns the result into the .icns.
import AppKit
import CoreGraphics

let size = 1024
guard CommandLine.arguments.count == 3,
      let art = NSImage(contentsOfFile: CommandLine.arguments[1]),
      let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                                    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                    isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0,
                                    bitsPerPixel: 0),
      let graphics = NSGraphicsContext(bitmapImageRep: bitmap) else {
    fatalError("Usage: swift render_icon.swift art.png output.png")
}

func color(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: CGColorSpaceCreateDeviceRGB(), components: [red / 255, green / 255, blue / 255, alpha])!
}

// Apple's icon shape is a rounded square whose corners blend smoothly into its sides; a
// superellipse of degree 5 follows it to within a pixel at this size.
func squircle(_ rect: CGRect) -> CGPath {
    let path = CGMutablePath()
    let steps = 720
    for step in 0..<steps {
        let angle = Double(step) / Double(steps) * 2 * .pi
        let x = rect.midX + rect.width / 2 * copysign(pow(abs(cos(angle)), 0.4), cos(angle))
        let y = rect.midY + rect.height / 2 * copysign(pow(abs(sin(angle)), 0.4), sin(angle))
        step == 0 ? path.move(to: CGPoint(x: x, y: y)) : path.addLine(to: CGPoint(x: x, y: y))
    }
    path.closeSubpath()
    return path
}

NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = graphics
let context = graphics.cgContext
context.setAllowsAntialiasing(true)
context.setShouldAntialias(true)
context.interpolationQuality = .high

let tileRect = CGRect(x: 100, y: 100, width: 824, height: 824)
let tile = squircle(tileRect)
context.saveGState()
context.setShadow(offset: CGSize(width: 0, height: -10), blur: 22, color: color(0, 0, 0, 0.32))
context.addPath(tile)
context.setFillColor(color(10, 18, 32))
context.fillPath()
context.restoreGState()

context.saveGState()
context.addPath(tile)
context.clip()
art.draw(in: tileRect.insetBy(dx: -16, dy: -16), from: .zero, operation: .sourceOver, fraction: 1)
// A thin light rim keeps the edge crisp against dark Dock and Finder backgrounds.
context.addPath(tile)
context.setStrokeColor(color(255, 255, 255, 0.14))
context.setLineWidth(4)
context.strokePath()
context.restoreGState()

guard let png = bitmap.representation(using: .png, properties: [:]) else { fatalError("Could not render PNG") }
try png.write(to: URL(fileURLWithPath: CommandLine.arguments[2]), options: .atomic)
NSGraphicsContext.restoreGraphicsState()
