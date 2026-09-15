import SwiftUI

/// First launch: pick the server and the camera. Pairing itself happens on the
/// first connection, when iOS asks for the passkey the camera displays.
struct SetupView: View {
    let model: AppModel
    @State private var serverURL: String

    init(model: AppModel) {
        self.model = model
        _serverURL = State(initialValue: model.serverBaseURL)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                HStack {
                    Caption("Little Camera")
                    Spacer()
                }
                .card()
                Rule()
                VStack(alignment: .leading, spacing: 10) {
                    Caption("Server")
                    Field(placeholder: "https://…", text: $serverURL, keyboard: .URL)
                    Text("The address of your blog's server. Photos are uploaded there in the camera's name; you never sign in here.")
                        .font(Theme.small)
                }
                .card()
                Rule()
                VStack(alignment: .leading, spacing: 10) {
                    Caption(model.bluetoothState == .poweredOn ? "Looking for camera" : model.linkLine)
                    Text("Hold the camera near this phone and press its shutter so it wakes up. It appears below as lc- and four letters, the same name it shows on its own screen.")
                    ForEach(model.found) { camera in
                        HStack {
                            Text(camera.name)
                                .font(Theme.font(14, bold: true))
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
                if !model.log.lines.isEmpty {
                    LogSection(model: model)
                    Rule()
                }
            }
        }
        .background(Theme.paper)
    }
}
