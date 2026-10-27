import SwiftUI

/// First launch: the server to upload to, and which camera is yours. Pairing itself happens on the first connection.
struct SetupStage: View {
    let model: AppModel
    @State private var serverURL: String

    init(model: AppModel) {
        self.model = model
        _serverURL = State(initialValue: model.serverURL)
    }

    var body: some View {
        VStack(spacing: 0) {
            StageHeader(title: "Little Camera", code: nil)
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    VStack(alignment: .leading, spacing: 10) {
                        Caption("Server")
                        Field(placeholder: "https://…", text: $serverURL, keyboard: .URL)
                        Text("Where your blog lives. Photos are uploaded in the camera's name, so you never sign in here.")
                            .font(Theme.small)
                    }
                    .card()
                    Rule()
                    VStack(alignment: .leading, spacing: 10) {
                        Caption(model.camera.bluetooth == .poweredOn ? "Looking for camera" : model.camera.line)
                        Text("Hold the camera near this phone and press its shutter so it wakes up. It appears below as lc- and four letters, the same name it shows on its own screen.")
                        ForEach(model.camera.found) { camera in
                            HStack {
                                Text(camera.name).font(Theme.font(14, bold: true))
                                Spacer()
                                Button {
                                    model.useCamera(camera, serverURL: serverURL)
                                } label: {
                                    Caption("Use this camera")
                                }
                                .buttonStyle(BlackButton())
                            }
                            .padding(.top, 6)
                        }
                    }
                    .card()
                    Rule()
                }
            }
        }
    }
}
