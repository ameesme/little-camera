import SwiftUI

struct RootView: View {
    let model: AppModel

    var body: some View {
        Group {
            if model.isSetUp {
                MainView(model: model)
            } else {
                SetupView(model: model)
            }
        }
        .font(Theme.body)
        .foregroundStyle(Theme.ink)
        .tint(Theme.ink)
        .background(Theme.paper.ignoresSafeArea())
        // Black on white, always; dark mode would invert the whole idea.
        .preferredColorScheme(.light)
    }
}

/// One scroll, sections separated by 1 px rules, like the blog.
struct MainView: View {
    let model: AppModel

    var body: some View {
        ScrollView {
            VStack(spacing: 0) {
                HeaderBar(model: model)
                Rule()
                CameraCard(model: model)
                Rule()
                SyncCard(model: model)
                Rule()
                BlogCard(model: model)
                Rule()
                SubscribersSection(model: model)
                Rule()
                LogSection(model: model)
                Rule()
            }
        }
        .background(Theme.paper)
    }
}

struct HeaderBar: View {
    let model: AppModel

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Caption("Little Camera")
            Spacer()
            // The short code is the fallback for linking on the web (protocol §5).
            Text(model.shortCode ?? "······")
                .font(Theme.font(14, bold: true))
                .kerning(2)
        }
        .card()
    }
}

struct CameraCard: View {
    let model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Caption(model.linkLine)
            if let info = model.info {
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
    }
}

struct SyncCard: View {
    let model: AppModel

    private static let clock: DateFormatter = {
        let f = DateFormatter()
        f.dateStyle = .short
        f.timeStyle = .short
        return f
    }()

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Caption("Sync")
            StatRow(label: "Last sync", value: model.lastSyncAt.map { SyncCard.clock.string(from: $0) } ?? "Never")
            StatRow(label: "Uploaded", value: "\(model.uploadedCount)")
            ProgressRule(fraction: model.progressFraction)
            if !model.recentThumbnails.isEmpty {
                HStack(spacing: 6) {
                    ForEach(Array(model.recentThumbnails.enumerated()), id: \.offset) { _, image in
                        Thumbnail(image: image)
                    }
                }
            }
            Button {
                model.syncNow()
            } label: {
                Caption("Sync now")
            }
            .buttonStyle(BlackButton())
            if let error = model.lastError {
                Text(error)
                    .font(Theme.small)
            }
        }
        .card()
    }
}

/// Progress bar: 1 px frame, filled with the checker.
struct ProgressRule: View {
    let fraction: Double

    var body: some View {
        GeometryReader { geo in
            HStack(spacing: 0) {
                Dither()
                    .frame(width: max(0, min(1, fraction)) * geo.size.width)
                Spacer(minLength: 0)
            }
        }
        .frame(height: 8)
        .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
    }
}

/// A 1-bit photo, drawn without interpolation so the dither stays a dither.
struct Thumbnail: View {
    let image: CGImage

    var body: some View {
        Image(decorative: image, scale: 1)
            .resizable()
            .interpolation(.none)
            .aspectRatio(4.0 / 3.0, contentMode: .fit)
            .frame(width: 80)
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
    }
}

struct LogSection: View {
    let model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Caption("Log")
            if model.log.lines.isEmpty {
                Text("Nothing yet.")
                    .font(Theme.small)
            }
            ForEach(Array(model.log.lines.enumerated()), id: \.offset) { _, line in
                Text(line)
                    .font(Theme.small)
                    .lineLimit(2)
            }
        }
        .card()
    }
}
