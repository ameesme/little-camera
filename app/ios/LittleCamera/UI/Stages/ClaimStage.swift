import SwiftUI

/// This camera already posts to a blog, so the phone holding it has to prove the blog is theirs.
/// Holding the camera is not proof of that — only the owner's inbox is (§4.9).
struct ClaimStage: View {
    let model: AppModel

    var body: some View {
        DeviceStage(title: "Prove it is yours", mood: .mail) {
            if let bound = model.blog.bound {
                Text(bound.url.withoutScheme)
                    .font(Theme.font(20, bold: true))
            }
            Text(model.blog.askedToBeLetIn
                 ? "Check that little blog's inbox. The link works for an hour."
                 : "Only the owner's inbox can let this phone in.")
            if model.blog.askedToBeLetIn {
                Waiting(text: "Waiting for you")
            }
            if let error = model.blog.error {
                Text(error).font(Theme.small)
            }
        } actions: {
            if model.blog.askedToBeLetIn {
                Button { model.refresh() } label: { Caption("Check again") }
                    .buttonStyle(BlackButton())
            } else {
                Button { model.blog.askToBeLetIn() } label: { Caption("Email me the link") }
                    .buttonStyle(BlackButton())
            }
            Button { model.disconnect() } label: { Caption("Use a different one") }
                .buttonStyle(QuietButton())
        }
    }
}

extension String {
    /// `https://mees.lttl.cam` reads better as `mees.lttl.cam` on a screen this size.
    var withoutScheme: String {
        replacingOccurrences(of: "https://", with: "").replacingOccurrences(of: "http://", with: "")
    }
}
