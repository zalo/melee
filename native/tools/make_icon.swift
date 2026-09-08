// Build a complete macOS icon family from the supplied SSBM logo.
import AppKit

let output = CommandLine.arguments[2]
let source = NSImage(contentsOfFile: CommandLine.arguments[1])!
try FileManager.default.createDirectory(atPath: output, withIntermediateDirectories: true)
for (points, scale) in [(16,1), (16,2), (32,1), (32,2), (128,1), (128,2), (256,1), (256,2), (512,1), (512,2)] {
    let size = points * scale
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    let factor = CGFloat(size) / 1024
    let transform = NSAffineTransform()
    transform.scale(by: factor)
    transform.concat()
    let tile = NSBezierPath(roundedRect: NSRect(x: 42, y: 42, width: 940, height: 940), xRadius: 208, yRadius: 208)
    NSGradient(starting: NSColor(calibratedRed: 0.18, green: 0.19, blue: 0.24, alpha: 1),
               ending: NSColor(calibratedRed: 0.025, green: 0.03, blue: 0.05, alpha: 1))!.draw(in: tile, angle: -90)
    NSColor(calibratedWhite: 0.5, alpha: 0.3).setStroke()
    tile.lineWidth = 3
    tile.stroke()
    let height = 860 * source.size.height / source.size.width
    source.draw(in: NSRect(x: 82, y: (1024 - height) / 2, width: 860, height: height),
                from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()
    let suffix = scale == 2 ? "@2x" : ""
    try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "\(output)/icon_\(points)x\(points)\(suffix).png"))
}
