import UIKit

/// A round translucent button that reports whether a finger is on it (multi-touch safe).
final class GlassButton: UIView {
    fileprivate let icon = UIImageView()
    var roundedRect = false { didSet { setNeedsLayout() } }
    private var fingers = Set<UITouch>()
    private(set) var isPressed = false { didSet { if isPressed != oldValue { refresh() } } }

    init(symbol: String) {
        super.init(frame: .zero)
        isMultipleTouchEnabled = true
        layer.borderWidth = 2.5
        icon.image = UIImage(systemName: symbol, withConfiguration: UIImage.SymbolConfiguration(weight: .heavy))
        icon.tintColor = .white
        icon.contentMode = .scaleAspectFit
        addSubview(icon)
        refresh()
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    override func layoutSubviews() {
        super.layoutSubviews()
        layer.cornerRadius = roundedRect ? min(bounds.width, bounds.height) * 0.3 : bounds.width / 2
        let m = min(bounds.width, bounds.height) * 0.28
        icon.frame = bounds.insetBy(dx: bounds.width / 2 - min(bounds.width, bounds.height) / 2 + m, dy: bounds.height / 2 - min(bounds.width, bounds.height) / 2 + m)
    }

    /// Extra touch area around the drawn circle, as a fraction of its diameter (the target is larger than the graphic).
    static let hitSlop: CGFloat = 0.3

    private func refresh() {
        // Mostly see-through while idle so the level stays visible; clearer while held.
        backgroundColor = UIColor(white: isPressed ? 1.0 : 0.1, alpha: isPressed ? 0.42 : 0.16)
        layer.borderColor = UIColor(white: 1, alpha: isPressed ? 0.9 : 0.42).cgColor
        icon.alpha = isPressed ? 1 : 0.62
        transform = isPressed ? CGAffineTransform(scaleX: 0.93, y: 0.93) : .identity
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { fingers.formUnion(touches); isPressed = true }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { fingers.subtract(touches); isPressed = !fingers.isEmpty }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { fingers.subtract(touches); isPressed = !fingers.isEmpty }
}

/// Four (or two, sideways only) arrow pads; a finger may slide from one to another.
final class DPadView: UIView {
    var horizontalOnly = false { didSet { if horizontalOnly != oldValue { active.removeAll(); rebuildMask(); setNeedsLayout() } } }
    private(set) var bits: Int32 = 0
    private var active: [UITouch: Int32] = [:]
    private let pads: [Int32: GlassButton] = [1: GlassButton(symbol: "chevron.left"), 2: GlassButton(symbol: "chevron.right"),
                                              4: GlassButton(symbol: "chevron.up"), 8: GlassButton(symbol: "chevron.down")]

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        for pad in pads.values { pad.isUserInteractionEnabled = false; addSubview(pad) }
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    /// The pads are drawn inside the view; the border of `slop` points around them still counts as the d-pad (bigger touch area).
    static let slop: CGFloat = 22

    override func layoutSubviews() {
        super.layoutSubviews()
        let r = bounds.insetBy(dx: DPadView.slop, dy: DPadView.slop)
        let w = r.width, h = r.height
        if horizontalOnly {
            let gap: CGFloat = 10, pw = (w - gap) / 2
            pads[1]!.frame = CGRect(x: r.minX, y: r.minY, width: pw, height: h)
            pads[2]!.frame = CGRect(x: r.maxX - pw, y: r.minY, width: pw, height: h)
            pads[4]!.isHidden = true; pads[8]!.isHidden = true
        } else {
            let gap: CGFloat = 6, p = (min(w, h) - 2 * gap) / 3
            let cx = r.midX - p / 2, cy = r.midY - p / 2
            pads[4]!.frame = CGRect(x: cx, y: r.minY, width: p, height: p)
            pads[8]!.frame = CGRect(x: cx, y: r.maxY - p, width: p, height: p)
            pads[1]!.frame = CGRect(x: r.minX, y: cy, width: p, height: p)
            pads[2]!.frame = CGRect(x: r.maxX - p, y: cy, width: p, height: p)
            pads[4]!.isHidden = false; pads[8]!.isHidden = false
        }
        for pad in pads.values { pad.roundedRect = horizontalOnly }
        refreshPads()
    }

    private func direction(at p: CGPoint) -> Int32 {
        let dx = p.x - bounds.midX, dy = p.y - bounds.midY
        let ax = abs(dx), ay = abs(dy)
        var m: Int32 = 0
        if horizontalOnly {
            if ax > bounds.width * 0.04 { m = dx < 0 ? 1 : 2 }
            return m
        }
        if ax < bounds.width * 0.08 && ay < bounds.height * 0.08 { return 0 }
        if ax >= ay * 0.35 { m |= dx < 0 ? 1 : 2 }
        if ay >= ax * 0.35 { m |= dy < 0 ? 4 : 8 }
        return m
    }

    private func rebuildMask() {
        bits = active.values.reduce(0, |)
        refreshPads()
    }

    private func refreshPads() {
        for (bit, pad) in pads {
            let on = bits & bit != 0
            pad.transform = on ? CGAffineTransform(scaleX: 0.93, y: 0.93) : .identity
            pad.backgroundColor = UIColor(white: on ? 1 : 0.1, alpha: on ? 0.42 : 0.16)
            pad.layer.borderColor = UIColor(white: 1, alpha: on ? 0.9 : 0.42).cgColor
            pad.icon.alpha = on ? 1 : 0.62
        }
    }

    private func track(_ touches: Set<UITouch>) {
        for t in touches { active[t] = direction(at: t.location(in: self)) }
        rebuildMask()
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { track(touches) }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { track(touches) }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { for t in touches { active[t] = nil }; rebuildMask() }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { for t in touches { active[t] = nil }; rebuildMask() }
}

/// The shell's in-level controls on top of the game view: d-pad, jump / crouch buttons, a settings panel.
/// Touches that do not hit a control fall through to the game.
final class ControlsOverlay: UIView {
    private let dpad = DPadView()
    private let jump = GlassButton(symbol: "arrow.up")
    private let crouch = GlassButton(symbol: "arrow.down")
    private let gear = UIButton(type: .system)
    private let panel = UIVisualEffectView(effect: UIBlurEffect(style: .systemThinMaterialDark))
    private let movementPicker = UISegmentedControl(items: ["Joystick", "Buttons"])
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
        var m: Int32 = s.movement == .buttons ? dpad.bits : 0
        if s.showJump && jump.isPressed { m |= 4 }
        if s.showCrouch && crouch.isPressed { m |= 8 }
        return m
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        for v in [dpad, jump, crouch, itemTap] as [UIView] { addSubview(v) }
        gear.setImage(UIImage(systemName: "gearshape.fill", withConfiguration: UIImage.SymbolConfiguration(pointSize: 18, weight: .bold)), for: .normal)
        gear.tintColor = .white
        gear.backgroundColor = UIColor(white: 0.15, alpha: 0.4)
        gear.layer.cornerRadius = 20
        gear.layer.borderWidth = 1.5
        gear.layer.borderColor = UIColor(white: 1, alpha: 0.6).cgColor
        gear.addTarget(self, action: #selector(togglePanel), for: .touchUpInside)
        addSubview(gear)
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
        panel.layer.cornerRadius = 16
        panel.clipsToBounds = true
        panel.isHidden = true
        func label(_ text: String, size: CGFloat = 15, weight: UIFont.Weight = .semibold) -> UILabel {
            let l = UILabel(); l.text = text; l.textColor = .white; l.font = .systemFont(ofSize: size, weight: weight); return l
        }
        func row(_ text: String, _ toggle: UISwitch) -> UIStackView {
            toggle.onTintColor = UIColor(red: 0.25, green: 0.6, blue: 1, alpha: 1)
            let r = UIStackView(arrangedSubviews: [label(text, weight: .regular), toggle]); r.distribution = .equalSpacing; r.alignment = .center; return r
        }
        movementPicker.addTarget(self, action: #selector(movementChanged), for: .valueChanged)
        horizontalSwitch.addTarget(self, action: #selector(switchChanged), for: .valueChanged)
        jumpSwitch.addTarget(self, action: #selector(switchChanged), for: .valueChanged)
        crouchSwitch.addTarget(self, action: #selector(switchChanged), for: .valueChanged)
        itemSwitch.addTarget(self, action: #selector(switchChanged), for: .valueChanged)
        controllerLabel.font = .systemFont(ofSize: 12); controllerLabel.textColor = UIColor(white: 1, alpha: 0.7)
        let note = label("Sideways only needs the jump button.", size: 12, weight: .regular); note.textColor = UIColor(white: 1, alpha: 0.7)
        let stack = UIStackView(arrangedSubviews: [label("Controls", size: 17, weight: .bold), movementPicker, row("Sideways only", horizontalSwitch), note,
                                                   label("Buttons"), row("Jump", jumpSwitch), row("Crouch / super jump", crouchSwitch), row("Item", itemSwitch), controllerLabel])
        stack.axis = .vertical; stack.spacing = 10
        stack.translatesAutoresizingMaskIntoConstraints = false
        panel.contentView.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo: panel.contentView.leadingAnchor, constant: 16),
                                     stack.trailingAnchor.constraint(equalTo: panel.contentView.trailingAnchor, constant: -16),
                                     stack.topAnchor.constraint(equalTo: panel.contentView.topAnchor, constant: 14),
                                     stack.bottomAnchor.constraint(equalTo: panel.contentView.bottomAnchor, constant: -14)])
        addSubview(panel)
    }

    @objc private func togglePanel() {
        panel.isHidden.toggle(); setNeedsLayout() }
    @objc private func movementChanged() { store.update { $0.movement = movementPicker.selectedSegmentIndex == 1 ? .buttons : .joystick } }
    @objc private func switchChanged() {
        store.update {
            $0.horizontalOnly = horizontalSwitch.isOn; $0.showJump = jumpSwitch.isOn; $0.showCrouch = crouchSwitch.isOn; $0.showItem = itemSwitch.isOn
        }
        settingsChanged()                                                       // normalisation may have changed a switch back
    }

    func setControllerStatus(_ connected: Bool) { controllerLabel.text = connected ? "Game controller connected" : "No game controller" }

    private func settingsChanged() {
        let s = store.settings
        movementPicker.selectedSegmentIndex = s.movement == .buttons ? 1 : 0
        horizontalSwitch.isOn = s.horizontalOnly
        jumpSwitch.isOn = s.showJump; jumpSwitch.isEnabled = !s.horizontalOnly
        crouchSwitch.isOn = s.showCrouch; itemSwitch.isOn = s.showItem
        dpad.horizontalOnly = s.horizontalOnly
        pr3_hud_config(s.movement == .joystick ? 1 : 0, s.showItem ? 1 : 0, s.horizontalOnly ? 1 : 0)
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
    /// Invisible, generous touch area over the original item button: it uses the held item (the game's own button is hidden under it).
    private let itemTap = ItemTapView()

    /// Sizes in points. The drawn circles are 60-75% of their touch areas (see GlassButton.hitSlop).
    private struct Metrics {
        let u: CGFloat                                   // scale: 1 on a 402 pt tall (iPhone) landscape screen
        var jump: CGFloat { 78 * u }
        var crouch: CGFloat { 62 * u }
        var item: CGFloat { 84 * u }                     // touch area of the item button
        var gap: CGFloat { 14 * u }
        var edge: CGFloat { 22 * u }                     // distance to the screen edge (and the notch)
        var pad: CGFloat { 58 * u }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let s = store.settings
        let m = Metrics(u: max(0.8, min(1.6, bounds.height / 402)))
        let k = bounds.height / 320                                     // points per game point
        let safe = safeAreaInsets
        let show = inLevel && !touchControlsHidden
        gear.isHidden = !inLevel
        dpad.isHidden = !(show && s.movement == .buttons)
        jump.isHidden = !(show && s.showJump)
        crouch.isHidden = !(show && s.showCrouch)
        itemTap.isHidden = !(show && s.showItem)
        let right = bounds.width - max(safe.right, 0) - m.edge, bottom = bounds.height - max(safe.bottom, 8) - m.edge * 0.8
        let left = max(safe.left, 0) + m.edge
        // Settings gear: top right, under the timer.
        let g: CGFloat = 36 * m.u
        gear.frame = CGRect(x: right - g + 6 * m.u, y: 74 * k, width: g, height: g)
        gear.layer.cornerRadius = g / 2
        gear.alpha = 0.55
        if !panel.isHidden {
            let size = panel.contentView.systemLayoutSizeFitting(CGSize(width: 290, height: UIView.layoutFittingCompressedSize.height),
                                                                 withHorizontalFittingPriority: .required, verticalFittingPriority: .fittingSizeLevel)
            panel.frame = CGRect(x: gear.frame.maxX - 290, y: gear.frame.maxY + 8, width: 290, height: min(size.height, bounds.height - gear.frame.maxY - 16))
        }
        // Left thumb: the d-pad, at the bottom left (where the thumbstick is).
        let cross = 3 * m.pad + 2 * 6, slop = DPadView.slop
        let dw: CGFloat = s.horizontalOnly ? 2 * (m.pad * 1.45) + 10 : cross, dh: CGFloat = s.horizontalOnly ? m.pad * 1.45 : cross
        dpad.frame = CGRect(x: left - slop, y: bottom - dh - slop, width: dw + 2 * slop, height: dh + 2 * slop)
        // Right thumb: slots in an arc around the bottom right corner, most used first: 0 corner, 1 above it, 2 to its left.
        let c0 = CGPoint(x: right - m.jump / 2, y: bottom - m.jump / 2)
        let slots = [c0,
                     CGPoint(x: c0.x, y: c0.y - (m.jump / 2 + m.gap + m.item / 2)),
                     CGPoint(x: c0.x - (m.jump / 2 + m.gap + m.crouch / 2), y: c0.y + (m.jump - m.crouch) / 2)]
        var order: [String] = []
        if s.showJump { order.append("jump") }
        if s.showItem { order.append("item") }
        if s.showCrouch { order.append("crouch") }
        var itemCenter = CGPoint(x: right - m.item / 2, y: bottom - m.item / 2)
        for (i, name) in order.enumerated() {
            let c = i == 0 && name == "item" ? CGPoint(x: right - m.item / 2, y: bottom - m.item / 2) : slots[i]
            switch name {
            case "jump": jump.frame = CGRect(x: c.x - m.jump / 2, y: c.y - m.jump / 2, width: m.jump, height: m.jump)
            case "crouch": crouch.frame = CGRect(x: c.x - m.crouch / 2, y: c.y - m.crouch / 2, width: m.crouch, height: m.crouch)
            default: itemCenter = c
            }
        }
        itemTap.frame = CGRect(x: itemCenter.x - m.item / 2, y: itemCenter.y - m.item / 2, width: m.item, height: m.item)
        // The original item button (drawn by the game) sits under the touch area.
        pr3_hud_item_pos(Double(itemCenter.x / k), Double(itemCenter.y / k))
    }

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        if !panel.isHidden && panel.frame.contains(point) { return panel.hitTest(convert(point, to: panel), with: event) ?? panel }
        if !gear.isHidden && gear.frame.insetBy(dx: -8, dy: -8).contains(point) { return gear }
        if !dpad.isHidden && dpad.frame.contains(point) { return dpad }
        // Round buttons: the touch area is the circle grown by hitSlop; where two overlap the nearer centre wins.
        var best: (UIView, CGFloat)?
        for v in [jump, crouch, itemTap] as [UIView] where !v.isHidden {
            let grow = v.bounds.width * GlassButton.hitSlop / 2
            guard v.frame.insetBy(dx: -grow, dy: -grow).contains(point) else { continue }
            let d = hypot(point.x - v.frame.midX, point.y - v.frame.midY)
            if best == nil || d < best!.1 { best = (v, d) }
        }
        return best?.0
    }
}

/// Transparent button: pressing it uses the held item.
final class ItemTapView: UIView {
    override init(frame: CGRect) { super.init(frame: frame); isMultipleTouchEnabled = true }
    required init?(coder: NSCoder) { fatalError("not used") }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { pr3_use_item() }
}
