import UIKit

// MARK: - Glass controls

/// A round control in frosted glass: blurred material, a soft highlight, a hairline edge and an SF Symbol.
/// Mostly see-through while idle, clearer and slightly smaller while held (with a light tap of haptics).
class GlassCircle: UIView {
    fileprivate let blur = UIVisualEffectView(effect: UIBlurEffect(style: .systemUltraThinMaterialDark))
    private let sheen = CAGradientLayer()
    fileprivate let icon = UIImageView()
    var cornerFraction: CGFloat? { didSet { setNeedsLayout() } }       // nil = circle, else corner radius as a fraction of the short side
    var symbolScale: CGFloat = 0.34 { didSet { setNeedsLayout() } }

    init(symbol: String?) {
        super.init(frame: .zero)
        layer.shadowColor = UIColor.black.cgColor
        layer.shadowOpacity = 0.28
        layer.shadowRadius = 10
        layer.shadowOffset = CGSize(width: 0, height: 4)
        blur.isUserInteractionEnabled = false
        blur.clipsToBounds = true
        addSubview(blur)
        sheen.colors = [UIColor(white: 1, alpha: 0.26).cgColor, UIColor(white: 1, alpha: 0.03).cgColor]
        sheen.startPoint = CGPoint(x: 0.3, y: 0); sheen.endPoint = CGPoint(x: 0.7, y: 1)
        blur.layer.addSublayer(sheen)
        blur.layer.borderWidth = 1
        blur.layer.borderColor = UIColor(white: 1, alpha: 0.38).cgColor
        icon.tintColor = .white
        icon.contentMode = .center
        icon.isUserInteractionEnabled = false
        addSubview(icon)
        if let symbol { setSymbol(symbol) }
        setPressed(false, animated: false)
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    func setSymbol(_ name: String) {
        symbolName = name
        setNeedsLayout()
    }
    private var symbolName = ""

    override func layoutSubviews() {
        super.layoutSubviews()
        blur.frame = bounds
        sheen.frame = blur.bounds
        let radius = cornerFraction.map { min(bounds.width, bounds.height) * $0 } ?? min(bounds.width, bounds.height) / 2
        blur.layer.cornerRadius = radius
        sheen.cornerRadius = radius
        layer.shadowPath = UIBezierPath(roundedRect: bounds, cornerRadius: radius).cgPath
        icon.frame = bounds
        let size = min(bounds.width, bounds.height) * symbolScale
        icon.image = symbolName.isEmpty ? nil : UIImage(systemName: symbolName, withConfiguration: UIImage.SymbolConfiguration(pointSize: size, weight: .semibold))
    }

    func setPressed(_ on: Bool, animated: Bool = true) {
        let apply = {
            self.alpha = on ? 1.0 : 0.62
            self.icon.alpha = on ? 1.0 : 0.82
            self.blur.layer.borderColor = UIColor(white: 1, alpha: on ? 0.8 : 0.38).cgColor
            self.transform = on ? CGAffineTransform(scaleX: 0.92, y: 0.92) : .identity
        }
        if animated { UIView.animate(withDuration: 0.22, delay: 0, usingSpringWithDamping: 0.75, initialSpringVelocity: 0, options: [.allowUserInteraction, .beginFromCurrentState], animations: apply) }
        else { apply() }
    }
}

/// A glass button that reports whether a finger is on it (multi-touch safe).
final class GlassButton: GlassCircle {
    /// Extra touch area around the drawn circle, as a fraction of its diameter (the target is larger than the graphic).
    static let hitSlop: CGFloat = 0.3
    var onPress: (() -> Void)?
    private var fingers = Set<UITouch>()
    private let haptic = UIImpactFeedbackGenerator(style: .light)
    private(set) var isPressed = false { didSet { if isPressed != oldValue { setPressed(isPressed) } } }

    override init(symbol: String?) { super.init(symbol: symbol); isMultipleTouchEnabled = true }
    required init?(coder: NSCoder) { fatalError("not used") }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        let was = isPressed
        fingers.formUnion(touches); isPressed = true
        if !was { haptic.impactOccurred(intensity: 0.7); onPress?() }
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { fingers.subtract(touches); isPressed = !fingers.isEmpty }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { fingers.subtract(touches); isPressed = !fingers.isEmpty }
}

/// Analogue-style thumbstick: a glass base and a knob that follows the finger; reports left / right / up / down past a threshold.
final class JoystickView: UIView {
    var horizontalOnly = false { didSet { if horizontalOnly != oldValue { reset() } } }
    private(set) var bits: Int32 = 0
    private let base = GlassCircle(symbol: nil)
    private let knob = GlassCircle(symbol: nil)
    private let arrows = [UIImageView(), UIImageView(), UIImageView(), UIImageView()]        // left, right, up, down
    private var touch: UITouch?
    /// The base is drawn this far inside the view; the margin around it is extra touch area.
    static let slop: CGFloat = 34

    override init(frame: CGRect) {
        super.init(frame: frame)
        addSubview(base)
        for (i, name) in ["chevron.left", "chevron.right", "chevron.up", "chevron.down"].enumerated() {
            arrows[i].image = UIImage(systemName: name, withConfiguration: UIImage.SymbolConfiguration(pointSize: 15, weight: .bold))
            arrows[i].tintColor = .white; arrows[i].alpha = 0.4; arrows[i].contentMode = .center
            base.addSubview(arrows[i])
        }
        knob.alpha = 0.9
        addSubview(knob)
        base.setPressed(false, animated: false)
        knob.setPressed(false, animated: false)
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    private var baseFrame: CGRect { bounds.insetBy(dx: JoystickView.slop, dy: JoystickView.slop) }
    private var travel: CGFloat { baseFrame.width * 0.5 - knobSize / 2 }
    private var knobSize: CGFloat { baseFrame.width * 0.42 }

    override func layoutSubviews() {
        super.layoutSubviews()
        base.frame = baseFrame
        let a = base.bounds, inset = a.width * 0.1
        arrows[0].frame = CGRect(x: inset, y: a.midY - 12, width: 24, height: 24)
        arrows[1].frame = CGRect(x: a.width - inset - 24, y: a.midY - 12, width: 24, height: 24)
        arrows[2].frame = CGRect(x: a.midX - 12, y: inset, width: 24, height: 24)
        arrows[3].frame = CGRect(x: a.midX - 12, y: a.height - inset - 24, width: 24, height: 24)
        arrows[2].isHidden = horizontalOnly; arrows[3].isHidden = horizontalOnly
        if touch == nil { placeKnob(.zero) }
    }

    private func placeKnob(_ offset: CGPoint) {
        knob.bounds = CGRect(origin: .zero, size: CGSize(width: knobSize, height: knobSize))
        knob.center = CGPoint(x: baseFrame.midX + offset.x, y: baseFrame.midY + offset.y)
    }

    private func update(_ t: UITouch) {
        let p = t.location(in: self)
        var dx = p.x - baseFrame.midX, dy = horizontalOnly ? 0 : p.y - baseFrame.midY
        let d = hypot(dx, dy)
        if d > travel { dx *= travel / d; dy *= travel / d }
        placeKnob(CGPoint(x: dx, y: dy))
        var m: Int32 = 0
        let thr = travel * 0.42
        if dx < -thr { m |= 1 }
        if dx > thr { m |= 2 }
        if dy < -thr { m |= 4 }
        if dy > thr { m |= 8 }
        bits = m
        for (i, bit) in [Int32(1), 2, 4, 8].enumerated() { arrows[i].alpha = m & bit != 0 ? 1 : 0.4 }
    }

    private func reset() {
        touch = nil; bits = 0
        placeKnob(.zero)
        for a in arrows { a.alpha = 0.4 }
        base.setPressed(false); knob.setPressed(false)
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard touch == nil, let t = touches.first else { return }
        touch = t; update(t); base.setPressed(true); knob.setPressed(true)
        UIImpactFeedbackGenerator(style: .light).impactOccurred(intensity: 0.6)
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { if let t = touch, touches.contains(t) { update(t) } }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { if let t = touch, touches.contains(t) { reset() } }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { if let t = touch, touches.contains(t) { reset() } }
}

/// Four (or two, sideways only) arrow pads; a finger may slide from one to another.
final class DPadView: UIView {
    var horizontalOnly = false { didSet { if horizontalOnly != oldValue { active.removeAll(); rebuildBits(); setNeedsLayout() } } }
    private(set) var bits: Int32 = 0
    private var active: [UITouch: Int32] = [:]
    private let pads: [Int32: GlassCircle] = [1: GlassCircle(symbol: "chevron.left"), 2: GlassCircle(symbol: "chevron.right"),
                                              4: GlassCircle(symbol: "chevron.up"), 8: GlassCircle(symbol: "chevron.down")]
    /// The pads are drawn inside the view; the border of `slop` points around them still counts as the d-pad (bigger touch area).
    static let slop: CGFloat = 26

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        for pad in pads.values { pad.isUserInteractionEnabled = false; addSubview(pad) }
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    override func layoutSubviews() {
        super.layoutSubviews()
        let r = bounds.insetBy(dx: DPadView.slop, dy: DPadView.slop)
        if horizontalOnly {
            let gap: CGFloat = 12, pw = (r.width - gap) / 2
            pads[1]!.frame = CGRect(x: r.minX, y: r.minY, width: pw, height: r.height)
            pads[2]!.frame = CGRect(x: r.maxX - pw, y: r.minY, width: pw, height: r.height)
            pads[4]!.isHidden = true; pads[8]!.isHidden = true
        } else {
            let gap: CGFloat = 8, p = (min(r.width, r.height) - 2 * gap) / 3
            let cx = r.midX - p / 2, cy = r.midY - p / 2
            pads[4]!.frame = CGRect(x: cx, y: r.minY, width: p, height: p)
            pads[8]!.frame = CGRect(x: cx, y: r.maxY - p, width: p, height: p)
            pads[1]!.frame = CGRect(x: r.minX, y: cy, width: p, height: p)
            pads[2]!.frame = CGRect(x: r.maxX - p, y: cy, width: p, height: p)
            pads[4]!.isHidden = false; pads[8]!.isHidden = false
        }
        for pad in pads.values { pad.cornerFraction = horizontalOnly ? 0.32 : nil }
    }

    private func direction(at p: CGPoint) -> Int32 {
        let dx = p.x - bounds.midX, dy = p.y - bounds.midY
        let ax = abs(dx), ay = abs(dy)
        if horizontalOnly { return ax > bounds.width * 0.04 ? (dx < 0 ? 1 : 2) : 0 }
        if ax < bounds.width * 0.08 && ay < bounds.height * 0.08 { return 0 }
        var m: Int32 = 0
        if ax >= ay * 0.35 { m |= dx < 0 ? 1 : 2 }
        if ay >= ax * 0.35 { m |= dy < 0 ? 4 : 8 }
        return m
    }

    private func rebuildBits() {
        let old = bits
        bits = active.values.reduce(0, |)
        for (bit, pad) in pads { pad.setPressed(bits & bit != 0) }
        if bits & ~old != 0 { UIImpactFeedbackGenerator(style: .light).impactOccurred(intensity: 0.5) }
    }

    private func track(_ touches: Set<UITouch>) {
        for t in touches { active[t] = direction(at: t.location(in: self)) }
        rebuildBits()
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { track(touches) }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { track(touches) }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { for t in touches { active[t] = nil }; rebuildBits() }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { for t in touches { active[t] = nil }; rebuildBits() }
}

// MARK: - Overlay

/// The shell's in-level controls on top of the game view: thumbstick or d-pad, jump / crouch / item buttons, a settings panel.
/// Touches that do not hit a control fall through to the game.
final class ControlsOverlay: UIView {
    private let joystick = JoystickView()
    private let dpad = DPadView()
    private let jump = GlassButton(symbol: "xmark")
    private let crouch = GlassButton(symbol: "circle")
    private let item = GlassButton(symbol: "square")
    private let gear = GlassButton(symbol: "gearshape.fill")
    private let panel = UIVisualEffectView(effect: UIBlurEffect(style: .systemThinMaterialDark))
    private let movementPicker = UISegmentedControl(items: ["Joystick", "Buttons"])
    private let originalSwitch = UISwitch()
    private let horizontalSwitch = UISwitch(), jumpSwitch = UISwitch(), crouchSwitch = UISwitch(), itemSwitch = UISwitch()
    private let controllerLabel = UILabel()
    private let store = ControlStore.shared
    private var inLevel = false
    /// Set while a game controller is in use: the touch controls step aside until the screen is touched again.
    private(set) var touchControlsHidden = false

    /// Touch input as the game's bit mask (bit0 left, bit1 right, bit2 up, bit3 down).
    var touchMask: Int32 {
        guard inLevel, !touchControlsHidden else { return 0 }
        let s = store.settings
        if s.original { return 0 }                                      // the game reads its own thumbstick
        var m: Int32 = s.movement == .buttons ? dpad.bits : joystick.bits
        if s.showJump && jump.isPressed { m |= 4 }
        if s.showCrouch && crouch.isPressed { m |= 8 }
        return m
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        for v in [joystick, dpad, jump, crouch, item, gear] as [UIView] { addSubview(v) }
        jump.symbolScale = 0.36; crouch.symbolScale = 0.36; item.symbolScale = 0.34; gear.symbolScale = 0.5
        item.onPress = { pr3_use_item() }
        gear.onPress = { [weak self] in self?.togglePanel() }
        buildPanel()
        store.onChange = { [weak self] in self?.settingsChanged() }
        settingsChanged()
        #if DEBUG
        if UserDefaults.standard.bool(forKey: "showPanel") { panel.isHidden = false }     // launch argument: -showPanel 1
        #endif
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    // MARK: Settings panel
    private func buildPanel() {
        panel.layer.cornerRadius = 22
        panel.layer.cornerCurve = .continuous
        panel.clipsToBounds = true
        panel.layer.borderWidth = 1
        panel.layer.borderColor = UIColor(white: 1, alpha: 0.18).cgColor
        panel.isHidden = true
        func label(_ text: String, size: CGFloat = 15, weight: UIFont.Weight = .semibold, alpha: CGFloat = 1) -> UILabel {
            let l = UILabel(); l.text = text; l.textColor = UIColor(white: 1, alpha: alpha); l.font = .systemFont(ofSize: size, weight: weight); return l
        }
        func row(_ text: String, _ toggle: UISwitch) -> UIStackView {
            toggle.onTintColor = UIColor.systemGreen
            let r = UIStackView(arrangedSubviews: [label(text, weight: .regular), toggle]); r.distribution = .equalSpacing; r.alignment = .center; return r
        }
        movementPicker.addTarget(self, action: #selector(movementChanged), for: .valueChanged)
        for sw in [originalSwitch, horizontalSwitch, jumpSwitch, crouchSwitch, itemSwitch] { sw.addTarget(self, action: #selector(switchChanged), for: .valueChanged) }
        controllerLabel.font = .systemFont(ofSize: 12); controllerLabel.textColor = UIColor(white: 1, alpha: 0.6)
        let stack = UIStackView(arrangedSubviews: [label("Controls", size: 18, weight: .bold), row("Original controls", originalSwitch),
                                                   label("The game's own thumbstick and item button.", size: 12, weight: .regular, alpha: 0.6), movementPicker,
                                                   row("Sideways only", horizontalSwitch),
                                                   label("Steer left and right; jumping uses its own button.", size: 12, weight: .regular, alpha: 0.6),
                                                   label("Buttons", size: 13, weight: .semibold, alpha: 0.6),
                                                   row("Jump", jumpSwitch), row("Crouch / super jump", crouchSwitch), row("Item", itemSwitch), controllerLabel])
        stack.axis = .vertical; stack.spacing = 11
        stack.translatesAutoresizingMaskIntoConstraints = false
        panel.contentView.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo: panel.contentView.leadingAnchor, constant: 18),
                                     stack.trailingAnchor.constraint(equalTo: panel.contentView.trailingAnchor, constant: -18),
                                     stack.topAnchor.constraint(equalTo: panel.contentView.topAnchor, constant: 16),
                                     stack.bottomAnchor.constraint(equalTo: panel.contentView.bottomAnchor, constant: -16)])
        addSubview(panel)
    }

    private func togglePanel() { panel.isHidden.toggle(); setNeedsLayout() }
    @objc private func movementChanged() { store.update { $0.movement = movementPicker.selectedSegmentIndex == 1 ? .buttons : .joystick } }
    @objc private func switchChanged() {
        store.update {
            $0.original = originalSwitch.isOn; $0.horizontalOnly = horizontalSwitch.isOn; $0.showJump = jumpSwitch.isOn; $0.showCrouch = crouchSwitch.isOn; $0.showItem = itemSwitch.isOn
        }
        settingsChanged()                                                       // normalisation may have changed a switch back
    }

    func setControllerStatus(_ connected: Bool) { controllerLabel.text = connected ? "Game controller connected" : "No game controller" }

    private func settingsChanged() {
        let s = store.settings
        originalSwitch.isOn = s.original
        movementPicker.selectedSegmentIndex = s.movement == .buttons ? 1 : 0
        horizontalSwitch.isOn = s.horizontalOnly
        jumpSwitch.isOn = s.showJump; jumpSwitch.isEnabled = !s.horizontalOnly
        crouchSwitch.isOn = s.showCrouch; itemSwitch.isOn = s.showItem
        // With the original controls every other option is off (and dimmed).
        let others = !s.original
        for control in [movementPicker, horizontalSwitch, crouchSwitch, itemSwitch] as [UIControl] { control.isEnabled = others; control.alpha = others ? 1 : 0.35 }
        jumpSwitch.isEnabled = others && !s.horizontalOnly; jumpSwitch.alpha = others ? 1 : 0.35
        pr3_hud_config(s.original ? 1 : 0, s.original ? 1 : 0, 0)       // the original thumbstick and item button: only in "original controls"
        pr3_hud_item_pos(-1, -1)
        dpad.horizontalOnly = s.horizontalOnly
        joystick.horizontalOnly = s.horizontalOnly
        setNeedsLayout()
    }

    // MARK: State
    func update(inLevel level: Bool) {
        guard level != inLevel else { return }
        inLevel = level
        if !level { panel.isHidden = true }
        setNeedsLayout()
    }

    func setTouchControlsHidden(_ hidden: Bool) {
        guard hidden != touchControlsHidden else { return }
        touchControlsHidden = hidden
        setNeedsLayout()
    }

    // MARK: Layout
    /// Sizes in points; `u` is 1 on a 402 pt tall (iPhone) landscape screen. The drawn circles are smaller than their touch areas.
    private struct Metrics {
        let u: CGFloat
        var jump: CGFloat { 84 * u }
        var item: CGFloat { 70 * u }
        var crouch: CGFloat { 64 * u }
        var gap: CGFloat { 14 * u }
        var edge: CGFloat { 26 * u }
        var stick: CGFloat { 150 * u }
        var pad: CGFloat { 56 * u }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let s = store.settings
        let m = Metrics(u: max(0.8, min(1.6, bounds.height / 402)))
        let k = bounds.height / 320                                     // points per game point
        let safe = safeAreaInsets
        let show = inLevel && !touchControlsHidden && !s.original
        gear.isHidden = !inLevel
        joystick.isHidden = !(show && s.movement == .joystick)
        dpad.isHidden = !(show && s.movement == .buttons)
        jump.isHidden = !(show && s.showJump)
        crouch.isHidden = !(show && s.showCrouch)
        item.isHidden = !(show && s.showItem)
        let right = bounds.width - max(safe.right, 0) - m.edge
        let bottom = bounds.height - max(safe.bottom, 8) - m.edge * 0.7
        let left = max(safe.left, 0) + m.edge
        // Settings gear: right beside the timer (the game draws it at the top right).
        let g = 34 * m.u
        gear.frame = CGRect(x: bounds.width - max(safe.right, 0) - 10 * k - 76 * m.u - g / 2, y: 25 * k - g / 2, width: g, height: g)
        if !panel.isHidden {
            let size = panel.contentView.systemLayoutSizeFitting(CGSize(width: 300, height: UIView.layoutFittingCompressedSize.height),
                                                                 withHorizontalFittingPriority: .required, verticalFittingPriority: .fittingSizeLevel)
            panel.frame = CGRect(x: gear.frame.maxX - 300, y: gear.frame.maxY + 10, width: 300, height: min(size.height, bounds.height - gear.frame.maxY - 16))
        }
        // Left thumb: thumbstick or d-pad in the bottom left corner.
        let slopJ = JoystickView.slop, slopD = DPadView.slop
        joystick.frame = CGRect(x: left - slopJ, y: bottom - m.stick - slopJ, width: m.stick + 2 * slopJ, height: m.stick + 2 * slopJ)
        let dw: CGFloat = s.horizontalOnly ? 2 * m.pad * 1.5 + 12 : 3 * m.pad + 16, dh: CGFloat = s.horizontalOnly ? m.pad * 1.5 : 3 * m.pad + 16
        dpad.frame = CGRect(x: left - slopD, y: bottom - dh - slopD, width: dw + 2 * slopD, height: dh + 2 * slopD)
        // Right thumb: an arc around the bottom right corner, most used first: jump in the corner, item up-left of it, crouch above it.
        func place(_ v: UIView, _ d: CGFloat, _ c: CGPoint) { v.frame = CGRect(x: c.x - d / 2, y: c.y - d / 2, width: d, height: d) }
        let diameters: [String: CGFloat] = ["jump": m.jump, "item": m.item, "crouch": m.crouch]
        var order: [String] = []
        if s.showJump { order.append("jump") }
        if s.showItem { order.append("item") }
        if s.showCrouch { order.append("crouch") }
        guard let first = order.first else { return }
        let r0 = diameters[first]!
        let c0 = CGPoint(x: right - r0 / 2, y: bottom - r0 / 2)
        for (i, name) in order.enumerated() {
            let d = diameters[name]!
            var c = c0
            if i > 0 {                                                   // on a circle around the corner button, at 160° and 105°
                let dist = (r0 + d) / 2 + m.gap
                let angle = (i == 1 ? 168.0 : 108.0) * CGFloat.pi / 180
                c = CGPoint(x: c0.x + dist * cos(angle), y: c0.y - dist * sin(angle))
            }
            switch name {
            case "jump": place(jump, d, c)
            case "crouch": place(crouch, d, c)
            default: place(item, d, c)
            }
        }
    }

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        if !panel.isHidden && panel.frame.contains(point) { return panel.hitTest(convert(point, to: panel), with: event) ?? panel }
        if !joystick.isHidden && joystick.frame.contains(point) { return joystick }
        if !dpad.isHidden && dpad.frame.contains(point) { return dpad }
        // Round buttons: the touch area is the circle grown by hitSlop; where two overlap the nearer centre wins.
        var best: (UIView, CGFloat)?
        for v in [jump, crouch, item, gear] as [UIView] where !v.isHidden {
            let grow = v.bounds.width * GlassButton.hitSlop / 2
            guard v.frame.insetBy(dx: -grow, dy: -grow).contains(point) else { continue }
            let d = hypot(point.x - v.frame.midX, point.y - v.frame.midY)
            if best == nil || d < best!.1 { best = (v, d) }
        }
        return best?.0
    }
}
