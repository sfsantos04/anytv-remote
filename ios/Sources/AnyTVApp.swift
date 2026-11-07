import SwiftUI

@main
struct AnyTVApp: App {
    @StateObject private var store = DeviceStore()

    var body: some Scene {
        WindowGroup {
            DevicesView()
                .environmentObject(store)
                .preferredColorScheme(.dark)
        }
    }
}
