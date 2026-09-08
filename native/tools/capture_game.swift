// Capture only the window belonging to the test process, without activating it.
import Foundation
import CoreGraphics

guard CommandLine.arguments.count == 3,
      let owner = Int(CommandLine.arguments[1]),
      let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]],
      let window = windows.first(where: {
          ($0[kCGWindowOwnerPID as String] as? Int) == owner &&
          ($0[kCGWindowLayer as String] as? Int) == 0
      }),
      let id = window[kCGWindowNumber as String] as? Int else {
    fputs("Test process has no visible game window\n", stderr)
    exit(1)
}
let capture = Process()
capture.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
capture.arguments = ["-x", "-t", "png", "-l", String(id), CommandLine.arguments[2]]
try capture.run()
capture.waitUntilExit()
exit(capture.terminationStatus)
