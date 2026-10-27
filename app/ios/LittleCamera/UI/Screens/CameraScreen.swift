import SwiftUI

/// The camera and the sync: what it holds, what has gone up, and the button for doing it now.
struct CameraScreen: View {
    let model: AppModel

    private static let clock: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .short
        formatter.timeStyle = .short
        return formatter
    }()

    private var camera: CameraSession { model.camera }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 10) {
                Caption(camera.line)
                if let info = camera.info {
                    StatRow(label: "Photos on camera", value: "\(info.photoCount)")
                    StatRow(label: "Unsynced", value: "\(info.unsyncedCount)")
                    StatRow(label: "Boot", value: "\(info.boot)")
                    StatRow(label: "Clock set", value: info.timeValid ? "Yes" : "No")
                } else {
                    Text("Press the shutter to wake the camera. It listens for half a minute; the phone does the rest by itself.")
                        .font(Theme.small)
                }
            }
            .card()
            Rule()
            VStack(alignment: .leading, spacing: 10) {
                Caption("Sync")
                StatRow(label: "Last sync", value: camera.lastSyncAt.map { CameraScreen.clock.string(from: $0) } ?? "Never")
                StatRow(label: "Uploaded", value: "\(camera.uploadedCount)")
                ProgressRule(fraction: camera.progress)
                if !camera.thumbnails.isEmpty {
                    HStack(spacing: 6) {
                        ForEach(Array(camera.thumbnails.enumerated()), id: \.offset) { _, image in
                            Thumbnail(image: image)
                        }
                    }
                }
                Button {
                    camera.syncNow()
                } label: {
                    Caption("Sync now")
                }
                .buttonStyle(BlackButton())
                if let error = camera.error {
                    Text(error).font(Theme.small)
                }
            }
            .card()
            Rule()
            BlogSummary(blog: model.blog)
            Rule()
        }
    }
}
