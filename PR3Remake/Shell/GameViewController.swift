import UIKit
import SwiftUI

/// Hosts the recompiled original game: boots it, runs the display link and keeps the rotated view letterboxed.
final class GameViewController: UIViewController {
    private var gameView: GLGameView!
    private var displayLink: CADisplayLink?
    private var lastTimestamp: CFTimeInterval = 0
    private var booted = false

    override func loadView() { view = UIView(); view.backgroundColor = .black }
    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        layoutGameView()
        if !booted && view.bounds.width > 0 { boot() }
    }

    private func layoutGameView() {
        guard let gameView else { return }
        let size = view.bounds.size
        // The game is 480 x 320 (landscape). Fit it inside the screen.
        let k = min(size.width / 480, size.height / 320)
        gameView.center = CGPoint(x: size.width / 2, y: size.height / 2)
        gameView.transform = CGAffineTransform(rotationAngle: -.pi / 2).scaledBy(x: k, y: k)
    }

    private func boot() {
        booted = true
        let scale: CGFloat = 3
        gameView = GLGameView(scale: scale)
        view.addSubview(gameView)
        layoutGameView()

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
            let px = 320 - e.y, py = e.x
            if !e.began && scriptTime >= e.start { script[i].began = true; pr3_touch(0, e.id, px, py) }
            else if e.began && scriptTime < e.end { pr3_touch(1, e.id, px, py) }
            else if e.began && scriptTime >= e.end { script[i].done = true; pr3_touch(3, e.id, px, py) }
        }
    }

    @objc private func tick(_ link: CADisplayLink) {
        let now = link.timestamp
        let dt = lastTimestamp == 0 ? 33.333 : (now - lastTimestamp) * 1000
        lastTimestamp = now
        runScript(dt: dt)
        pr3_frame(dt)
        if now - lastSave >= 10 { lastSave = now; pr3_save() }   // iOS can kill a suspended app without notice
    }
}

struct GameHost: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> GameViewController { GameViewController() }
    func updateUIViewController(_ uiViewController: GameViewController, context: Context) {}
}
