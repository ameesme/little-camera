import SwiftUI

/// Between the way in and the search. Creating the central is what makes iOS put its own alert up,
/// so this screen says what that alert is for while the owner can still read it.
struct PermissionStage: View {
    let model: AppModel

    var body: some View {
        DeviceStage(title: "It talks over Bluetooth") {
            Text("Little camera has no internet of its own, it needs this phone to send pictures. ok?")
        } actions: {
            Button { model.allowBluetooth() } label: { Caption("Allow Bluetooth") }
                .buttonStyle(BlackButton())
        }
    }
}
