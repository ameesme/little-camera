import SwiftUI

/// Paired with a camera, but the server has not tied it to a blog yet. One instruction, nothing else to do here.
struct LinkStage: View {
    let model: AppModel

    var body: some View {
        VStack(spacing: 0) {
            StageHeader(title: "Link your camera", code: model.camera.shortCode)
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Caption(model.camera.line)
                    Text("Open \(model.serverURL)/app/me on this phone. It shows a code; photograph it with the camera and press send.")
                    Text("If the camera is not recognised, type \(model.camera.shortCode ?? "the code above") on that page instead. It only works for a minute or two after the camera has uploaded something.")
                        .font(Theme.small)
                    if let error = model.blog.error {
                        Text(error).font(Theme.small)
                    }
                    Button {
                        model.refresh()
                    } label: {
                        Caption("Check again")
                    }
                    .buttonStyle(BlackButton())
                }
                .card()
            }
            Rule()
            LogStrip(log: model.log)
        }
    }
}
