import SwiftUI

/// Two tabs, because there are two things here: what the blog holds, and how the camera is doing.
struct MainTabs: View {
    let model: AppModel
    @State private var selection = Demo.onCameraTab ? 1 : 0

    var body: some View {
        TabView(selection: $selection) {
            Tab("Blog", systemImage: "square.stack", value: 0) {
                BlogTab(model: model)
            }
            Tab("Camera", systemImage: "camera", value: 1) {
                CameraTab(model: model)
            }
        }
        // Liquid Glass picks up the tint; black on white is the only palette here.
        .tint(Theme.ink)
        .tabBarMinimizeBehavior(.onScrollDown)
    }
}
