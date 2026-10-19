import UIKit
import SwiftUI

/// Hosts the recompiled original game: boots it, runs the display link and keeps the rotated view letterboxed.
final class GameViewController: UIViewController {
    private var gameView: GLGameView!
    private var displayLink: CADisplayLink?
    private var lastTimestamp: CFTimeInterval = 0
    private var booted = false
    /// Points added on each side of the game's 480 x 320 landscape screen so that it fills the display (0 on 4:3 screens).
    private var wideOff = 0
    private var overlay: ControlsOverlay!
    private let gamepad = GamepadInput()
    private let cursorView = UIView(frame: CGRect(x: 0, y: 0, width: 22, height: 22))
    private var cursor = CGPoint.zero
    private var cursorDown = false
    private var itemHeld = false

    override func loadView() { view = UIView(); view.backgroundColor = .black }
    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        layoutGameView()
        updateSafeArea()
        if !booted && view.bounds.width > view.bounds.height { boot() }      // wait for landscape: the wide layout depends on the display aspect
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        // The game is landscape only: ask for it explicitly (the SwiftUI host does not always rotate by itself).
        setNeedsUpdateOfSupportedInterfaceOrientations()
        view.window?.windowScene?.requestGeometryUpdate(.iOS(interfaceOrientations: .landscape)) { _ in }
    }

    override func viewSafeAreaInsetsDidChange() {
        super.viewSafeAreaInsetsDidChange()
        updateSafeArea()
    }

    /// The notch / rounded corners in game points: the in-level controls stay clear of them.
    private func updateSafeArea() {
        let k = max(0.1, min(view.bounds.width / 480, view.bounds.height / 320))
        let insets = view.safeAreaInsets
        pr3_set_safe(Int32(insets.left / k), Int32(insets.right / k))
    }

    private func layoutGameView() {
        guard let gameView else { return }
        let size = view.bounds.size
        // The game is 480 x 320 (landscape). Fit its height to the screen; the width is widened (wideOff) to fill it.
        let k = min(size.width / 480, size.height / 320)
        gameView.center = CGPoint(x: size.width / 2, y: size.height / 2)
        gameView.transform = CGAffineTransform(rotationAngle: -.pi / 2).scaledBy(x: k, y: k)
    }

    private func boot() {
        booted = true
        let scale: CGFloat = 3
        let size = view.bounds.size
        wideOff = max(0, Int(((size.width / size.height) * 320 - 480) / 2))
        pr3_set_wide(Int32(wideOff))
        gameView = GLGameView(scale: scale, wideOff: wideOff)
        view.addSubview(gameView)
        layoutGameView()
        updateSafeArea()
        overlay = ControlsOverlay(frame: view.bounds)
        overlay.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(overlay)
        gameView.onTouchBegan = { [weak self] in self?.overlay.setTouchControlsHidden(false) }
        cursorView.isHidden = true
        cursorView.isUserInteractionEnabled = false
        cursorView.backgroundColor = UIColor(white: 1, alpha: 0.85)
        cursorView.layer.cornerRadius = 11
        cursorView.layer.borderWidth = 2
        cursorView.layer.borderColor = UIColor(red: 0.1, green: 0.2, blue: 0.5, alpha: 1).cgColor
        view.addSubview(cursorView)
        cursor = CGPoint(x: view.bounds.midX, y: view.bounds.midY)
        overlay.setControllerStatus(gamepad.connected)
        gamepad.onConnectionChange = { [weak self] connected in self?.overlay.setControllerStatus(connected) }

        pr3_set_view_hooks({ GLGameView.current?.startUpdate() }, { GLGameView.current?.endUpdate() })
        let bundle = Bundle.main
        guard let data = bundle.resourcePath.map({ $0 + "/GameData" }) else { return }
        let docs = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0]
        #if DEBUG
        freopen(docs + "/pr3_log.txt", "w", stderr)   // engine warnings (unhandled imports etc.)
        setvbuf(stderr, nil, _IOLBF, 0)
        #endif
        let rc = pr3_boot(data + "/PR3.code", data, docs, Float(scale))
        NSLog("PR3: boot returned %d", rc)
        loadScript()
        let link = CADisplayLink(target: self, selector: #selector(tick(_:)))
        // The original ran at a fixed 30 fps (animationInterval = 1/30) and much of its logic counts frames.
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 30, preferred: 30)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    // MARK: Debug autopilot: -script "tap@12:240:160;hold@30-60:90:230" (seconds, 480x320 game coordinates)
    private struct ScriptEvent { var start: Double; var end: Double; var x: Float; var y: Float; var id: Int32; var began = false; var done = false }
    private var script: [ScriptEvent] = []
    private var scriptTime = 0.0
    private var lastSave = 0.0

    private func loadScript() {
        #if DEBUG
        guard let text = UserDefaults.standard.string(forKey: "script") else { return }
        for (i, part) in text.split(separator: ";").enumerated() {
            let a = part.split(separator: "@")
            guard a.count == 2 else { continue }
            let comps = a[1].split(separator: ":")
            guard comps.count >= 3, let x = Float(comps[1]), let y = Float(comps[2]) else { continue }
            let times = comps[0].split(separator: "-").compactMap { Double($0) }
            guard let t0 = times.first else { continue }
            let t1 = times.count > 1 ? times[1] : t0 + 0.12
            script.append(ScriptEvent(start: t0, end: t1, x: x, y: y, id: 9000 + Int32(i)))
        }
        #endif
    }

    private func runScript(dt: Double) {
        guard !script.isEmpty else { return }
        scriptTime += dt / 1000
        for i in script.indices where !script[i].done {
            let e = script[i]
            // game (landscape) point -> the view's local portrait point, like -[UITouch locationInView:]
            let px = 320 - e.y, py = e.x + Float(wideOff)
            if !e.began && scriptTime >= e.start { script[i].began = true; pr3_touch(0, e.id, px, py) }
            else if e.began && scriptTime < e.end { pr3_touch(1, e.id, px, py) }
            else if e.began && scriptTime >= e.end { script[i].done = true; pr3_touch(3, e.id, px, py) }
        }
    }

    /// Merges touch and game-controller input into the game for this frame; with a controller and no level, a cursor taps the menus.
    private func updateControls(dt: Double) {
        let inLevel = pr3_in_level() != 0
        overlay.update(inLevel: inLevel)
        let pad = gamepad.poll()
        if inLevel {
            releaseCursor()
            cursorView.isHidden = true
            if pad.mask != 0 || pad.item { overlay.setTouchControlsHidden(true) }
            pr3_input_set(overlay.touchMask | pad.mask)
            if pad.item && !itemHeld { pr3_use_item() }
            itemHeld = pad.item
            return
        }
        pr3_input_set(0)
        itemHeld = false
        guard gamepad.connected else { releaseCursor(); cursorView.isHidden = true; return }
        cursorView.isHidden = false
        func curve(_ v: CGFloat) -> CGFloat { let m = max(0, abs(v) - 0.18) / 0.82; return (v < 0 ? -1 : 1) * m * m }
        cursor.x = max(1, min(view.bounds.width - 1, cursor.x + curve(pad.cursor.x) * 520 * dt))
        cursor.y = max(1, min(view.bounds.height - 1, cursor.y + curve(pad.cursor.y) * 520 * dt))
        cursorView.center = cursor
        let local = gameView.convert(cursor, from: view)
        if pad.click && !cursorDown { cursorDown = true; pr3_touch(0, 9999, Float(local.x), Float(local.y)) }
        else if pad.click { pr3_touch(1, 9999, Float(local.x), Float(local.y)) }
        else if !pad.click && cursorDown { cursorDown = false; pr3_touch(3, 9999, Float(local.x), Float(local.y)) }
    }

    private func releaseCursor() {
        guard cursorDown else { return }
        cursorDown = false
        let local = gameView.convert(cursor, from: view)
        pr3_touch(3, 9999, Float(local.x), Float(local.y))
    }

    @objc private func tick(_ link: CADisplayLink) {
        let now = link.timestamp
        let dt = lastTimestamp == 0 ? 33.333 : (now - lastTimestamp) * 1000
        lastTimestamp = now
        runScript(dt: dt)
        updateControls(dt: dt / 1000)
        pr3_frame(dt)
        if now - lastSave >= 10 { lastSave = now; pr3_save() }   // iOS can kill a suspended app without notice
    }
}

struct GameHost: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> GameViewController { GameViewController() }
    func updateUIViewController(_ uiViewController: GameViewController, context: Context) {}
}
