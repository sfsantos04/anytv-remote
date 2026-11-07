import Foundation
import AVFoundation
import UIKit

/// Turns the Annex-B H.264 that the TV streams into sample buffers the display
/// layer can render. No files, no ffmpeg: VideoToolbox decodes on the device.
final class H264StreamPlayer: ObservableObject {
    let layer = AVSampleBufferDisplayLayer()

    @Published private(set) var frames: Int = 0
    @Published private(set) var kilobytesPerSecond: Double = 0
    @Published private(set) var lastError: String = ""

    private var bytes: [UInt8] = []
    private var sps: [UInt8]?
    private var pps: [UInt8]?
    private var format: CMVideoFormatDescription?
    private var pending: [[UInt8]] = []
    private let enqueueQueue = DispatchQueue(label: "anytv.h264.enqueue")
    private var windowStart = Date()
    private var windowFrames = 0
    private var windowBytes = 0

    init() {
        layer.videoGravity = .resizeAspect
        layer.backgroundColor = UIColor.black.cgColor
    }

    func reset() {
        bytes.removeAll(keepingCapacity: true)
        pending.removeAll(keepingCapacity: true)
        sps = nil
        pps = nil
        format = nil
        frames = 0
        windowFrames = 0
        windowBytes = 0
        windowStart = Date()
        enqueueQueue.async { [layer] in layer.flush() }
    }

    /// Feeds one chunk of the elementary stream.
    func feed(_ data: Data) {
        guard !data.isEmpty else { return }
        bytes.append(contentsOf: data)
        windowBytes += data.count
        scan()

        let now = Date()
        let elapsed = now.timeIntervalSince(windowStart)
        if elapsed >= 1.0 {
            kilobytesPerSecond = Double(windowBytes) / 1024.0 / elapsed
            windowBytes = 0
            windowStart = now
        }
    }

    /// Splits complete NAL units out of the buffer; the trailing partial unit stays.
    private func scan() {
        var starts: [Int] = []
        var i = 0
        let count = bytes.count
        while i + 3 <= count {
            if bytes[i] == 0, bytes[i + 1] == 0 {
                if bytes[i + 2] == 1 {
                    starts.append(i)
                    i += 3
                    continue
                }
                if i + 3 < count, bytes[i + 2] == 0, bytes[i + 3] == 1 {
                    starts.append(i)
                    i += 4
                    continue
                }
            }
            i += 1
        }
        guard starts.count >= 2 else { return }
        for k in 0..<(starts.count - 1) {
            let start = starts[k]
            let header = bytes[start + 2] == 1 ? 3 : 4
            let from = start + header
            let to = starts[k + 1]
            if to > from { absorb(Array(bytes[from..<to])) }
        }
        bytes.removeFirst(starts[starts.count - 1])
    }

    private func absorb(_ nal: [UInt8]) {
        guard let first = nal.first else { return }
        switch first & 0x1F {
        case 7:
            sps = nal
            rebuildFormat()
        case 8:
            pps = nal
            rebuildFormat()
        case 9, 6:
            pending.append(nal)
        default:
            // A coded slice closes the access unit; anything queued rides with it.
            pending.append(nal)
            emit(pending)
            pending.removeAll(keepingCapacity: true)
        }
    }

    private func rebuildFormat() {
        guard format == nil, let sps, let pps else { return }
        var description: CMVideoFormatDescription?
        sps.withUnsafeBufferPointer { s in
            pps.withUnsafeBufferPointer { p in
                var pointers: [UnsafePointer<UInt8>] = [s.baseAddress!, p.baseAddress!]
                var sizes: [Int] = [sps.count, pps.count]
                let status = pointers.withUnsafeMutableBufferPointer { ptrs in
                    sizes.withUnsafeMutableBufferPointer { sz in
                        CMVideoFormatDescriptionCreateFromH264ParameterSets(
                            allocator: kCFAllocatorDefault,
                            parameterSetCount: 2,
                            parameterSetPointers: ptrs.baseAddress!,
                            parameterSetSizes: sz.baseAddress!,
                            nalUnitHeaderLength: 4,
                            formatDescriptionOut: &description)
                    }
                }
                if status == noErr { format = description }
            }
        }
    }

    private func emit(_ nals: [[UInt8]]) {
        guard let format, !nals.isEmpty else { return }
        var avcc = [UInt8]()
        avcc.reserveCapacity(nals.reduce(0) { $0 + $1.count + 4 })
        for nal in nals {
            var length = UInt32(nal.count).bigEndian
            withUnsafeBytes(of: &length) { avcc.append(contentsOf: $0) }
            avcc.append(contentsOf: nal)
        }
        let total = avcc.count
        var blockBuffer: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(allocator: kCFAllocatorDefault,
                                                 memoryBlock: nil,
                                                 blockLength: total,
                                                 blockAllocator: kCFAllocatorDefault,
                                                 customBlockSource: nil,
                                                 offsetToData: 0,
                                                 dataLength: total,
                                                 flags: 0,
                                                 blockBufferOut: &blockBuffer) == kCMBlockBufferNoErr,
              let block = blockBuffer else { return }
        let copied = avcc.withUnsafeBytes { raw -> OSStatus in
            CMBlockBufferReplaceDataBytes(with: raw.baseAddress!,
                                          blockBuffer: block,
                                          offsetIntoDestination: 0,
                                          dataLength: total)
        }
        guard copied == kCMBlockBufferNoErr else { return }

        var sample: CMSampleBuffer?
        var timing = CMSampleTimingInfo(duration: CMTime(value: 1, timescale: 30),
                                        presentationTimeStamp: .invalid,
                                        decodeTimeStamp: .invalid)
        var size = total
        guard CMSampleBufferCreateReady(allocator: kCFAllocatorDefault,
                                        dataBuffer: block,
                                        formatDescription: format,
                                        sampleCount: 1,
                                        sampleTimingEntryCount: 1,
                                        sampleTimingArray: &timing,
                                        sampleSizeEntryCount: 1,
                                        sampleSizeArray: &size,
                                        sampleBufferOut: &sample) == noErr,
              let buffer = sample else { return }

        if let attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, createIfNecessary: true) {
            let dict = unsafeBitCast(CFArrayGetValueAtIndex(attachments, 0), to: CFMutableDictionary.self)
            CFDictionarySetValue(dict,
                                 Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                                 Unmanaged.passUnretained(kCFBooleanTrue).toOpaque())
        }

        enqueueQueue.async { [weak self] in
            guard let self else { return }
            if self.layer.status == .failed { self.layer.flush() }
            self.layer.enqueue(buffer)
            DispatchQueue.main.async {
                self.frames += 1
                self.windowFrames += 1
            }
        }
    }
}

/// Hosts the display layer inside SwiftUI.
struct LiveVideoView: UIViewRepresentable {
    let player: H264StreamPlayer

    func makeUIView(context: Context) -> UIView {
        let view = UIView()
        view.backgroundColor = .black
        player.layer.frame = view.bounds
        view.layer.addSublayer(player.layer)
        return view
    }

    func updateUIView(_ view: UIView, context: Context) {
        player.layer.frame = view.bounds
    }
}
