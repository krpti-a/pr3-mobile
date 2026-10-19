import Foundation

/// How the player steers in a level. The original had one thumbstick plus an item button; the shell adds a d-pad mode,
/// separate jump / crouch buttons, a "sideways only" steering mode and game-controller input.
struct ControlSettings: Equatable {
    enum Movement: Int { case joystick = 0, buttons = 1 }

    /// The original game's own thumbstick and item button; every other option is off then.
    var original = false
    var movement: Movement = .joystick
    /// The stick / d-pad only steers left and right; jumping (and crouching) then needs their own buttons.
    var horizontalOnly = false
    /// Jump (up) button.
    var showJump = false
    /// Crouch button: hold it to charge a super jump, then jump.
    var showCrouch = false
    /// The original item button (it uses the held item).
    var showItem = true

    /// Keeps the combination playable: without up on the stick there must be a jump button.
    mutating func normalize() { if horizontalOnly { showJump = true } }
}

/// Persistent control settings (UserDefaults); every change is pushed to the game's HUD configuration.
final class ControlStore {
    static let shared = ControlStore()
    private let defaults = UserDefaults.standard
    private(set) var settings = ControlSettings()
    var onChange: (() -> Void)?

    private init() {
        var s = ControlSettings()
        func has(_ key: String) -> Bool { defaults.object(forKey: key) != nil }
        if has("ctl.movement") { s.movement = ControlSettings.Movement(rawValue: defaults.integer(forKey: "ctl.movement")) ?? .joystick }
        if has("ctl.original") { s.original = defaults.bool(forKey: "ctl.original") }
        if has("ctl.horizontalOnly") { s.horizontalOnly = defaults.bool(forKey: "ctl.horizontalOnly") }
        if has("ctl.showJump") { s.showJump = defaults.bool(forKey: "ctl.showJump") }
        if has("ctl.showCrouch") { s.showCrouch = defaults.bool(forKey: "ctl.showCrouch") }
        if has("ctl.showItem") { s.showItem = defaults.bool(forKey: "ctl.showItem") }
        s.normalize()
        settings = s
    }

    func update(_ change: (inout ControlSettings) -> Void) {
        var s = settings
        change(&s)
        s.normalize()
        guard s != settings else { return }
        settings = s
        defaults.set(s.movement.rawValue, forKey: "ctl.movement")
        defaults.set(s.original, forKey: "ctl.original")
        defaults.set(s.horizontalOnly, forKey: "ctl.horizontalOnly")
        defaults.set(s.showJump, forKey: "ctl.showJump")
        defaults.set(s.showCrouch, forKey: "ctl.showCrouch")
        defaults.set(s.showItem, forKey: "ctl.showItem")
        onChange?()
    }
}
