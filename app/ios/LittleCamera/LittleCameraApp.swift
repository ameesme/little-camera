import SwiftUI

@main
struct LittleCameraApp: App {
    @State private var model: AppModel
    @Environment(\.scenePhase) private var scenePhase

    init() {
        // The CBCentralManager must exist right at launch: when iOS relaunches us in
        // the background for a pending connect it hands the restored state to the
        // manager with our restore identifier, and only if it is already there.
        let model = AppModel()
        model.start()
        _model = State(initialValue: model)
    }

    var body: some Scene {
        WindowGroup {
            RootView(model: model)
                .onChange(of: scenePhase) { _, phase in
                    if phase == .active {
                        model.refreshStatus()
                    }
                }
        }
    }
}
