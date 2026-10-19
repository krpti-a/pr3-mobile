import SwiftUI

@main
struct PR3App: App {
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            GameHost()
                .ignoresSafeArea()
                .statusBarHidden(true)
                .persistentSystemOverlays(.hidden)
        }
        // The original saved its settings/progress in applicationWillResignActive; iOS may kill a backgrounded app at any time.
        .onChange(of: scenePhase) { phase in
            if phase != .active { pr3_save() }
        }
    }
}
