import SwiftUI

@main
struct AnyTVApp: App {
    @StateObject private var store = DeviceStore()

    init() {
        // passa a registrar a causa se o app cair por sinal
        CrashReporter.install()
    }

    var body: some Scene {
        WindowGroup {
            DevicesView()
                .environmentObject(store)
                .preferredColorScheme(.dark)
        }
    }
}
