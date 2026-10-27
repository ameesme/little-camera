import SwiftUI

struct LogScreen: View {
    let log: SyncLog

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if log.lines.isEmpty {
                Text("Nothing yet.").font(Theme.small)
            }
            ForEach(Array(log.lines.enumerated()), id: \.offset) { _, line in
                Text(line)
                    .font(Theme.small)
                    .lineLimit(2)
            }
        }
        .card()
    }
}

/// The last few lines, for the stages that have no Log screen to send anyone to.
struct LogStrip: View {
    let log: SyncLog

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(Array(log.lines.prefix(3).enumerated()), id: \.offset) { _, line in
                Text(line)
                    .font(Theme.small)
                    .lineLimit(1)
            }
        }
        .card()
    }
}
