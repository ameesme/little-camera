import SwiftUI
import ImageIO

/// A blog photo at the dither's own scale: a still, or a motion photo (protocol §2.1) that loops.
/// The server sends a motion photo as a looping GIF (§4.7), and `AsyncImage` only ever shows the
/// first frame of one, so the frames are read out with ImageIO and stepped by a timeline at the
/// feed's `frame_ms`. A still is one frame and never redraws.
struct MotionImage: View {
    let url: URL?
    var frameMs: Int = 400
    @State private var frames: [CGImage] = []

    var body: some View {
        Group {
            if frames.count > 1 {
                let step = Double(max(frameMs, 50)) / 1000
                TimelineView(.periodic(from: .now, by: step)) { context in
                    let tick = Int(context.date.timeIntervalSinceReferenceDate / step)
                    picture(frames[tick % frames.count])
                }
            } else if let still = frames.first {
                picture(still)
            } else {
                Dither().aspectRatio(4.0 / 3.0, contentMode: .fit)
            }
        }
        .task(id: url) { frames = await Self.load(url) }
    }

    private func picture(_ image: CGImage) -> some View {
        Image(decorative: image, scale: 1)
            .resizable()
            .interpolation(.none)
            .aspectRatio(4.0 / 3.0, contentMode: .fit)
    }

    /// Every frame in the file: one for a PNG, all of them for a GIF. Empty when it cannot be had.
    private static func load(_ url: URL?) async -> [CGImage] {
        guard let url,
              let (data, _) = try? await URLSession.shared.data(from: url),
              let source = CGImageSourceCreateWithData(data as CFData, nil) else { return [] }
        return (0..<CGImageSourceGetCount(source)).compactMap { CGImageSourceCreateImageAtIndex(source, $0, nil) }
    }
}
