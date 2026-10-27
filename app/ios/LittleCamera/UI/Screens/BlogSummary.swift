import SwiftUI

/// The blog as it stands: its address, how much is on it, and the profile picture, which only the camera can take.
struct BlogSummary: View {
    let blog: Blog

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let bound = blog.bound {
                Caption("\(bound.name)'s little camera")
                if let url = URL(string: bound.url) {
                    Link(bound.url, destination: url)
                        .font(Theme.body)
                        .underline()
                }
                StatRow(label: "Photos", value: "\(bound.photoCount ?? 0)")
                StatRow(label: "Battery", value: bound.battery.map { "\($0) %" } ?? "—")
                Button {
                    blog.requestAvatar()
                } label: {
                    Caption(blog.awaitingAvatar ? "Next picture becomes your profile picture" : "Take your profile picture")
                }
                .buttonStyle(BlackButton())
                .disabled(blog.awaitingAvatar)
            }
            if let error = blog.error {
                Text(error).font(Theme.small)
            }
        }
        .card()
    }
}
