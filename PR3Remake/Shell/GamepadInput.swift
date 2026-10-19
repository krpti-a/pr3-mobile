import Foundation
import GameController

/// Game-controller input: steers and jumps in a level, and drives a cursor (a mouse-like pointer) in the menus.
final class GamepadInput {
    struct State {
        var mask: Int32 = 0              // bit0 left, bit1 right, bit2 up (jump), bit3 down (crouch)
        var item = false                 // item button held
        var click = false                // primary button (A) held, for the menu cursor
        var cursor = CGPoint.zero        // left stick / d-pad, -1...1
        var anyInput = false
    }

    private(set) var connected = false
    var onConnectionChange: ((Bool) -> Void)?

    init() {
        NotificationCenter.default.addObserver(self, selector: #selector(controllersChanged), name: .GCControllerDidConnect, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(controllersChanged), name: .GCControllerDidDisconnect, object: nil)
        GCController.startWirelessControllerDiscovery(completionHandler: {})
        controllersChanged()
    }

    @objc private func controllersChanged() {
        let now = GCController.controllers().contains { $0.extendedGamepad != nil }
        if now != connected { connected = now; onConnectionChange?(now) }
    }

    func poll() -> State {
        var s = State()
        for controller in GCController.controllers() {
            guard let pad = controller.extendedGamepad else { continue }
            let lx = pad.leftThumbstick.xAxis.value, ly = pad.leftThumbstick.yAxis.value
            let dx = pad.dpad.xAxis.value, dy = pad.dpad.yAxis.value
            let x = abs(dx) > 0.5 ? dx : lx, y = abs(dy) > 0.5 ? dy : ly          // +y is up
            if x < -0.35 { s.mask |= 1 }
            if x > 0.35 { s.mask |= 2 }
            if y > 0.5 || pad.buttonA.isPressed || pad.rightShoulder.isPressed { s.mask |= 4 }
            if y < -0.5 || pad.buttonB.isPressed || pad.leftShoulder.isPressed { s.mask |= 8 }
            if pad.buttonX.isPressed || pad.buttonY.isPressed || pad.rightTrigger.isPressed { s.item = true }
            if pad.buttonA.isPressed { s.click = true }
            s.cursor.x += CGFloat(abs(dx) > 0.2 ? dx : lx)
            s.cursor.y -= CGFloat(abs(dy) > 0.2 ? dy : ly)                          // screen y grows downwards
        }
        s.cursor.x = max(-1, min(1, s.cursor.x)); s.cursor.y = max(-1, min(1, s.cursor.y))
        s.anyInput = s.mask != 0 || s.item || s.click || abs(s.cursor.x) > 0.2 || abs(s.cursor.y) > 0.2
        return s
    }
}
