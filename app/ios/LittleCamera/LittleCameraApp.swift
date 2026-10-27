import SwiftUI

@main
struct LittleCameraApp: App {
    @State private var model: AppModel
    @Environment(\.scenePhase) private var scenePhase

    init() {
        // iOS hands a background relaunch's restored state to the manager with our identifier only if it already exists.
        let model = AppModel()
        model.start()
        _model = State(initialValue: model)
    }

    var body: some Scene {
        WindowGroup {
            RootView(model: model)
                .onChange(of: scenePhase) { _, phase in
                    if phase == .active {
                        model.refresh()
                    }
                }
        }
    }
}
