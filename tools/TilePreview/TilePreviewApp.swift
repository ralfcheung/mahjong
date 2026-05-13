import SwiftUI
import MetalKit

@main
struct TilePreviewApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 800, minHeight: 600)
        }
    }
}

struct ContentView: View {
    @State private var renderer: PreviewRenderer?
    @State private var orientation: PreviewRenderer.TileOrientation = .standing
    @State private var showWireframe = false
    @State private var showFaceTexture = true
    @State private var tileCount = 3

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 16) {
                Text("Tile Preview").font(.headline)

                Divider().frame(height: 20)

                Picker("Orientation", selection: $orientation) {
                    ForEach(PreviewRenderer.TileOrientation.allCases, id: \.self) { o in
                        Text(o.rawValue).tag(o)
                    }
                }
                .pickerStyle(.segmented)
                .frame(width: 300)

                Divider().frame(height: 20)

                Stepper("Tiles: \(tileCount)", value: $tileCount, in: 1...5)

                Toggle("Wireframe", isOn: $showWireframe)
                Toggle("Face Tex", isOn: $showFaceTexture)

                Spacer()

                Text("Drag: orbit | Scroll: zoom | Space: reset")
                    .font(.caption).foregroundColor(.secondary)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(Color(nsColor: .controlBackgroundColor))

            MetalView(renderer: $renderer)
                .onChange(of: orientation) { _, v in renderer?.orientation = v }
                .onChange(of: showWireframe) { _, v in renderer?.showWireframe = v }
                .onChange(of: showFaceTexture) { _, v in renderer?.showFaceTexture = v }
                .onChange(of: tileCount) { _, v in renderer?.tileCount = v }
                .onAppear {
                    // Sync initial state after renderer is created
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                        renderer?.orientation = orientation
                        renderer?.showFaceTexture = showFaceTexture
                        renderer?.tileCount = tileCount
                    }
                }
        }
    }
}

struct MetalView: NSViewRepresentable {
    @Binding var renderer: PreviewRenderer?

    func makeNSView(context: Context) -> KeyboardMTKView {
        let mtkView = KeyboardMTKView()
        mtkView.device = MTLCreateSystemDefaultDevice()
        mtkView.preferredFramesPerSecond = 60

        let r = PreviewRenderer(mtkView: mtkView)
        mtkView.delegate = r
        DispatchQueue.main.async { renderer = r }

        let coordinator = context.coordinator
        coordinator.renderer = r

        let panGesture = NSPanGestureRecognizer(target: coordinator, action: #selector(Coordinator.handlePan(_:)))
        mtkView.addGestureRecognizer(panGesture)

        let magnifyGesture = NSMagnificationGestureRecognizer(target: coordinator, action: #selector(Coordinator.handleMagnify(_:)))
        mtkView.addGestureRecognizer(magnifyGesture)

        mtkView.onScrollWheel = { [weak coordinator] deltaY in
            coordinator?.renderer?.camera.zoom(delta: deltaY * 0.5)
        }

        mtkView.onKeyDown = { [weak coordinator] keyCode in
            guard let r = coordinator?.renderer else { return }
            switch keyCode {
            case 49: r.camera.reset()  // Space
            default: break
            }
        }

        return mtkView
    }

    func updateNSView(_ nsView: KeyboardMTKView, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator() }

    class Coordinator: NSObject {
        var renderer: PreviewRenderer?

        @objc func handlePan(_ gesture: NSPanGestureRecognizer) {
            guard let renderer = renderer else { return }
            let translation = gesture.translation(in: gesture.view)
            let modifiers = NSEvent.modifierFlags

            if modifiers.contains(.shift) {
                renderer.camera.pan(deltaX: Float(translation.x), deltaY: Float(-translation.y))
            } else {
                renderer.camera.orbit(deltaX: Float(translation.x), deltaY: Float(-translation.y))
            }
            gesture.setTranslation(.zero, in: gesture.view)
        }

        @objc func handleMagnify(_ gesture: NSMagnificationGestureRecognizer) {
            renderer?.camera.zoom(delta: Float(gesture.magnification))
            gesture.magnification = 0
        }
    }
}

class KeyboardMTKView: MTKView {
    var onKeyDown: ((UInt16) -> Void)?
    var onScrollWheel: ((Float) -> Void)?

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        onKeyDown?(event.keyCode)
    }

    override func scrollWheel(with event: NSEvent) {
        onScrollWheel?(Float(event.deltaY))
    }
}
