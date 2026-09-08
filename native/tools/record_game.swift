// Record exactly one process window, even while another application is in front.
// Uses ScreenCaptureKit recording output (macOS 15), with no microphone capture.
import Foundation
import ScreenCaptureKit
import AVFoundation
import AppKit

final class RecordingDelegate: NSObject, SCRecordingOutputDelegate {
    var completed = false
    var failure: Error?
    func recordingOutputDidStartRecording(_ output: SCRecordingOutput) {}
    func recordingOutputDidFinishRecording(_ output: SCRecordingOutput) { completed = true }
    func recordingOutput(_ output: SCRecordingOutput, didFailWithError error: Error) {
        failure = error; completed = true
    }
}
@main struct Recorder {
    @MainActor static func main() async throws {
        _ = NSApplication.shared
        guard CommandLine.arguments.count == 4,
              let pid = Int32(CommandLine.arguments[1]),
              let seconds = UInt64(CommandLine.arguments[3]), seconds > 0, seconds <= 60 else {
            fatalError("Usage: record_game PID output.mp4 seconds (1-60)")
        }
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: false)
        guard let window = content.windows.first(where: { $0.owningApplication?.processID == pid && $0.windowLayer == 0 }) else {
            fatalError("Test process has no game window")
        }
        let filter = SCContentFilter(desktopIndependentWindow: window)
        let configuration = SCStreamConfiguration()
        configuration.width = 1280
        configuration.height = Int((1280 * window.frame.height / window.frame.width) / 2) * 2
        configuration.minimumFrameInterval = CMTime(value: 1, timescale: 60)
        configuration.showsCursor = false
        configuration.capturesAudio = false
        configuration.captureMicrophone = false
        let stream = SCStream(filter: filter, configuration: configuration, delegate: nil)
        let outputConfig = SCRecordingOutputConfiguration()
        outputConfig.outputURL = URL(fileURLWithPath: CommandLine.arguments[2])
        outputConfig.outputFileType = .mp4
        outputConfig.videoCodecType = .h264
        let delegate = RecordingDelegate()
        let output = SCRecordingOutput(configuration: outputConfig, delegate: delegate)
        try stream.addRecordingOutput(output)
        try await stream.startCapture()
        try await Task.sleep(nanoseconds: seconds * 1_000_000_000)
        try await stream.stopCapture()
        for _ in 0..<100 {
            if delegate.completed { break }
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        if let error = delegate.failure { throw error }
        guard delegate.completed else { fatalError("Recording did not finish") }
        print("Recorded game window \(window.windowID): \(outputConfig.outputURL.path)")
    }
}
