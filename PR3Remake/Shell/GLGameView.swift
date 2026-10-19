import UIKit
import OpenGLES
import QuartzCore

/// The OpenGL ES 1.1 view the original engine renders into (the original RK_EAGLView).
/// The framebuffer is the game's portrait 320 x 480 space; the view is rotated into landscape by its transform,
/// exactly like the original app, so `location(in:)` yields the same coordinates the game expects.
final class GLGameView: UIView {
    override class var layerClass: AnyClass { CAEAGLLayer.self }

    private var context: EAGLContext!
    private var framebuffer: GLuint = 0
    private var renderbuffer: GLuint = 0
    private var touchIDs: [ObjectIdentifier: Int32] = [:]
    private var nextTouchID: Int32 = 1000

    static weak var current: GLGameView?
    /// Called when a finger touches the game (the shell then shows its touch controls again after a game controller was used).
    var onTouchBegan: (() -> Void)?

    /// The game's portrait screen is 320 x 480 points. On a wider display the view is `wideOff` points taller on both sides
    /// (the landscape game area grows to the left and right, see pr3_set_wide).
    static func portraitSize(wideOff: Int) -> CGSize { CGSize(width: 320, height: 480 + 2 * CGFloat(wideOff)) }

    init(scale: CGFloat, wideOff: Int) {
        super.init(frame: CGRect(origin: .zero, size: GLGameView.portraitSize(wideOff: wideOff)))
        contentScaleFactor = scale
        isMultipleTouchEnabled = true
        isOpaque = true
        let eagl = layer as! CAEAGLLayer
        eagl.isOpaque = true
        eagl.drawableProperties = [kEAGLDrawablePropertyRetainedBacking: false,
                                   kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8]
        guard let ctx = EAGLContext(api: .openGLES1) else { fatalError("OpenGL ES 1.1 is not available") }
        context = ctx
        EAGLContext.setCurrent(ctx)
        createFramebuffer()
        GLGameView.current = self
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    private func createFramebuffer() {
        glGenFramebuffersOES(1, &framebuffer)
        glBindFramebufferOES(GLenum(GL_FRAMEBUFFER_OES), framebuffer)
        glGenRenderbuffersOES(1, &renderbuffer)
        glBindRenderbufferOES(GLenum(GL_RENDERBUFFER_OES), renderbuffer)
        context.renderbufferStorage(Int(GL_RENDERBUFFER_OES), from: layer as? CAEAGLLayer)
        glFramebufferRenderbufferOES(GLenum(GL_FRAMEBUFFER_OES), GLenum(GL_COLOR_ATTACHMENT0_OES),
                                     GLenum(GL_RENDERBUFFER_OES), renderbuffer)
        var w: GLint = 0, h: GLint = 0
        glGetRenderbufferParameterivOES(GLenum(GL_RENDERBUFFER_OES), GLenum(GL_RENDERBUFFER_WIDTH_OES), &w)
        glGetRenderbufferParameterivOES(GLenum(GL_RENDERBUFFER_OES), GLenum(GL_RENDERBUFFER_HEIGHT_OES), &h)
        NSLog("PR3: renderbuffer %dx%d (scale %.1f)", w, h, contentScaleFactor)
    }

    // Called by the engine (-[RK_EAGLView startUpdateView] / endUpdateView).
    func startUpdate() {
        EAGLContext.setCurrent(context)
        glBindFramebufferOES(GLenum(GL_FRAMEBUFFER_OES), framebuffer)
    }

    func endUpdate() {
        glBindRenderbufferOES(GLenum(GL_RENDERBUFFER_OES), renderbuffer)
        context.presentRenderbuffer(Int(GL_RENDERBUFFER_OES))
    }

    // MARK: Touches (RK_EAGLView touchesBegan/Moved/Ended/Cancelled)
    private func id(for t: UITouch) -> Int32 {
        let key = ObjectIdentifier(t)
        if let v = touchIDs[key] { return v }
        nextTouchID += 1
        touchIDs[key] = nextTouchID
        return nextTouchID
    }

    private func forward(_ touches: Set<UITouch>, phase: Int32, release: Bool = false) {
        for t in touches {
            let p = t.location(in: self)
            pr3_touch(phase, id(for: t), Float(p.x), Float(p.y))
            if release { touchIDs[ObjectIdentifier(t)] = nil }
        }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { onTouchBegan?(); forward(touches, phase: 0) }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { forward(touches, phase: 1) }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { forward(touches, phase: 3, release: true) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { forward(touches, phase: 4, release: true) }
}
