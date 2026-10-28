import SwiftUI

/// Two tabs, because there are two things here: what the blog holds, and how the camera is doing.
struct MainTabs: View {
    let model: AppModel

    var body: some View {
        TabView {
            Tab("Blog", systemImage: "square.stack") {
                BlogTab(model: model)
            }
            Tab("Camera", systemImage: "camera") {
                CameraTab(model: model)
            }
        }
        // Liquid Glass picks up the tint; black on white is the only palette here.
        .tint(Theme.ink)
        .tabBarMinimizeBehavior(.onScrollDown)
    }
}
