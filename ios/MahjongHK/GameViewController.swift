import UIKit
import GLKit

/// Main view controller for the Mahjong HK iOS app.
/// Owns the OpenGL ES view and drives the game loop via CADisplayLink.
class GameViewController: UIViewController {

    private var bridge: RaylibBridge!
    private var displayLink: CADisplayLink?
    private var lastFrameTime: CFTimeInterval = 0

    // Touch tracking
    private var activeTouches: [UITouch: CGPoint] = [:]
    private var lastTapTime: CFTimeInterval = 0
    private var lastTapLocation: CGPoint = .zero
    private let doubleTapInterval: CFTimeInterval = 0.35
    private let doubleTapDistance: CGFloat = 30.0

    // MARK: - Lifecycle

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black
        view.isMultipleTouchEnabled = true

        // Set up the GL view (Raylib creates its own on iOS)
        setupGame()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        startDisplayLink()
    }

    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        stopDisplayLink()
    }

    deinit {
        bridge?.shutdown()
    }

    // MARK: - Setup

    private func setupGame() {
        bridge = RaylibBridge()

        let screenSize = UIScreen.main.bounds.size
        let scale = UIScreen.main.scale
        let width = Int(screenSize.width * scale)
        let height = Int(screenSize.height * scale)

        guard let bundlePath = Bundle.main.resourcePath else {
            fatalError("Cannot find bundle resource path")
        }

        bridge.initWithWidth(Int32(width),
                            height: Int32(height),
                            scaleFactor: Float(scale),
                            bundlePath: bundlePath)
    }

    // MARK: - Display Link

    private func startDisplayLink() {
        stopDisplayLink()
        lastFrameTime = CACurrentMediaTime()
        displayLink = CADisplayLink(target: self, selector: #selector(frameLoop))
        displayLink?.preferredFramesPerSecond = 60
        displayLink?.add(to: .main, forMode: .common)
    }

    private func stopDisplayLink() {
        displayLink?.invalidate()
        displayLink = nil
    }

    @objc private func frameLoop() {
        let now = CACurrentMediaTime()
        let dt = Float(now - lastFrameTime)
        lastFrameTime = now

        bridge.updateAndRender(dt)
    }

    // MARK: - Touch Handling

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        let scale = UIScreen.main.scale
        for touch in touches {
            let loc = touch.location(in: view)
            activeTouches[touch] = loc
        }

        // Check for multi-touch
        if activeTouches.count == 2 {
            let pts = Array(activeTouches.values)
            bridge.handleTwoFingerGesture(Float(pts[0].x * scale),
                                         y1: Float(pts[0].y * scale),
                                         x2: Float(pts[1].x * scale),
                                         y2: Float(pts[1].y * scale))
        }
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        let scale = UIScreen.main.scale
        for touch in touches {
            activeTouches[touch] = touch.location(in: view)
        }

        if activeTouches.count == 2 {
            let pts = Array(activeTouches.values)
            bridge.handleTwoFingerGesture(Float(pts[0].x * scale),
                                         y1: Float(pts[0].y * scale),
                                         x2: Float(pts[1].x * scale),
                                         y2: Float(pts[1].y * scale))
        }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        let scale = UIScreen.main.scale

        // If going from 2 → fewer touches, end two-finger gesture
        let wasMulti = activeTouches.count >= 2

        for touch in touches {
            activeTouches.removeValue(forKey: touch)
        }

        if wasMulti {
            bridge.endTwoFingerGesture()
            return
        }

        // Single finger release → tap
        if let touch = touches.first, activeTouches.isEmpty {
            let loc = touch.location(in: view)
            let now = CACurrentMediaTime()

            // Double-tap detection
            let dist = hypot(loc.x - lastTapLocation.x, loc.y - lastTapLocation.y)
            if now - lastTapTime < doubleTapInterval && dist < doubleTapDistance {
                bridge.handleDoubleTap()
                lastTapTime = 0
                return
            }

            lastTapTime = now
            lastTapLocation = loc

            bridge.handleTap(Float(loc.x * scale), y: Float(loc.y * scale))
        }
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        activeTouches.removeAll()
        bridge.endTwoFingerGesture()
    }

    // MARK: - Status Bar

    override var prefersStatusBarHidden: Bool { true }
    override var preferredStatusBarStyle: UIStatusBarStyle { .lightContent }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
}
