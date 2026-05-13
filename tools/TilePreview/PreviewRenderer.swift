import MetalKit
import simd

/// MTKViewDelegate that renders 1-5 tiles for interactive inspection.
class PreviewRenderer: NSObject, MTKViewDelegate {

    let device: MTLDevice
    let commandQueue: MTLCommandQueue

    // Pipeline states
    private var tileBodyPipeline: MTLRenderPipelineState!
    private var texturedQuadPipeline: MTLRenderPipelineState!
    private var shadowPipeline: MTLRenderPipelineState!
    private var wireframePipeline: MTLRenderPipelineState!

    // Meshes
    private var standingMesh: TileMeshFactory.TileMesh!
    private var flatMesh: TileMeshFactory.TileMesh!

    // Atlas
    private let tileAtlas = TileAtlas()
    private var samplerState: MTLSamplerState!

    // Camera
    let camera = OrbitalCamera()

    // Display state
    enum TileOrientation: String, CaseIterable {
        case standing = "Standing"
        case flat = "Flat"
        case tilted = "Tilted"
        case back = "Back"
    }

    var orientation: TileOrientation = .standing
    var showWireframe = false
    var showFaceTexture = true
    var tileCount = 3

    // Assets path
    private let assetsPath: String

    init?(mtkView: MTKView) {
        guard let device = mtkView.device ?? MTLCreateSystemDefaultDevice() else { return nil }
        self.device = device
        mtkView.device = device
        guard let queue = device.makeCommandQueue() else { return nil }
        self.commandQueue = queue

        // Find assets path (relative to repo root)
        let repoRoot = "/Users/ralfcheung/code/mahjong"
        self.assetsPath = "\(repoRoot)/assets"

        super.init()

        mtkView.colorPixelFormat = .bgra8Unorm
        mtkView.depthStencilPixelFormat = .depth32Float
        mtkView.clearColor = MTLClearColor(red: 0.1, green: 0.1, blue: 0.12, alpha: 1)

        buildPipelines(mtkView: mtkView)
        buildMeshes()
        buildAtlas()
        buildSampler()
    }

    // MARK: - Setup

    private func buildPipelines(mtkView: MTKView) {
        // SPM doesn't compile .metal files, so we compile from source at runtime.
        // We preprocess the #include by inlining ShaderTypes.h contents.
        let library: MTLLibrary
        do {
            let srcDir = "/Users/ralfcheung/code/mahjong/tools/TilePreview"
            var shaderSource = try String(contentsOfFile: "\(srcDir)/Shaders.metal", encoding: .utf8)
            let headerSource = try String(contentsOfFile: "\(srcDir)/ShaderTypes.h", encoding: .utf8)
            shaderSource = shaderSource.replacingOccurrences(
                of: "#include \"ShaderTypes.h\"",
                with: headerSource
            )
            library = try device.makeLibrary(source: shaderSource, options: nil)
        } catch {
            fatalError("Failed to compile Metal shaders: \(error)")
        }

        // Shaders read from device buffers directly (no vertex descriptor needed)

        // Tile body pipeline
        let bodyDesc = MTLRenderPipelineDescriptor()
        bodyDesc.vertexFunction = library.makeFunction(name: "tileBodyVertex")
        bodyDesc.fragmentFunction = library.makeFunction(name: "tileBodyFragment")
        bodyDesc.colorAttachments[0].pixelFormat = mtkView.colorPixelFormat
        bodyDesc.depthAttachmentPixelFormat = mtkView.depthStencilPixelFormat
        tileBodyPipeline = try! device.makeRenderPipelineState(descriptor: bodyDesc)

        // Textured quad pipeline
        let texDesc = MTLRenderPipelineDescriptor()
        texDesc.vertexFunction = library.makeFunction(name: "texturedQuadVertex")
        texDesc.fragmentFunction = library.makeFunction(name: "texturedQuadFragment")
        texDesc.colorAttachments[0].pixelFormat = mtkView.colorPixelFormat
        texDesc.colorAttachments[0].isBlendingEnabled = true
        texDesc.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
        texDesc.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
        texDesc.depthAttachmentPixelFormat = mtkView.depthStencilPixelFormat
        texturedQuadPipeline = try! device.makeRenderPipelineState(descriptor: texDesc)

        // Shadow pipeline (alpha blended)
        let shadowDesc = MTLRenderPipelineDescriptor()
        shadowDesc.vertexFunction = library.makeFunction(name: "shadowVertex")
        shadowDesc.fragmentFunction = library.makeFunction(name: "shadowFragment")
        shadowDesc.colorAttachments[0].pixelFormat = mtkView.colorPixelFormat
        shadowDesc.colorAttachments[0].isBlendingEnabled = true
        shadowDesc.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
        shadowDesc.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
        shadowDesc.depthAttachmentPixelFormat = mtkView.depthStencilPixelFormat
        shadowPipeline = try! device.makeRenderPipelineState(descriptor: shadowDesc)

        // Wireframe pipeline (reuse tile body vertex, yellow fragment)
        let wireDesc = MTLRenderPipelineDescriptor()
        wireDesc.vertexFunction = library.makeFunction(name: "tileBodyVertex")
        wireDesc.fragmentFunction = library.makeFunction(name: "wireframeFragment")
        wireDesc.colorAttachments[0].pixelFormat = mtkView.colorPixelFormat
        wireDesc.depthAttachmentPixelFormat = mtkView.depthStencilPixelFormat
        wireframePipeline = try! device.makeRenderPipelineState(descriptor: wireDesc)
    }

    // Ground plane
    private var groundVertexBuffer: MTLBuffer!
    private var groundIndexBuffer: MTLBuffer!

    private func buildMeshes() {
        standingMesh = TileMeshFactory.buildStandingTileMesh(device: device)
        flatMesh = TileMeshFactory.buildFlatTileMesh(device: device)
        buildGroundPlane()
    }

    private func buildGroundPlane() {
        // Dark green ground plane at y=0
        let hs: Float = 3.0
        let c = SIMD4<Float>(0.12, 0.25, 0.15, 1.0)
        let n = SIMD3<Float>(0, 1, 0)
        var verts: [VertexIn] = []
        for (x, z) in [(-hs, -hs), (hs, -hs), (hs, hs), (-hs, hs)] as [(Float, Float)] {
            var v = VertexIn()
            v.position = SIMD3<Float>(x, 0, z)
            v.normal = n
            v.color = c
            v.texCoord = .zero
            verts.append(v)
        }
        let idxs: [UInt32] = [0, 1, 2, 0, 2, 3]
        groundVertexBuffer = device.makeBuffer(bytes: verts, length: verts.count * MemoryLayout<VertexIn>.stride, options: .storageModeShared)
        groundIndexBuffer = device.makeBuffer(bytes: idxs, length: idxs.count * MemoryLayout<UInt32>.stride, options: .storageModeShared)
    }

    private func buildAtlas() {
        tileAtlas.generate(device: device, assetsPath: assetsPath, tileVersion: "live")
    }

    private func buildSampler() {
        let desc = MTLSamplerDescriptor()
        desc.minFilter = .linear
        desc.magFilter = .linear
        desc.mipFilter = .linear
        desc.sAddressMode = .clampToEdge
        desc.tAddressMode = .clampToEdge
        samplerState = device.makeSamplerState(descriptor: desc)
    }

    // MARK: - MTKViewDelegate

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    private var frameCount = 0

    func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable,
              let rpd = view.currentRenderPassDescriptor,
              let cmdBuf = commandQueue.makeCommandBuffer() else { return }

        frameCount += 1
        let aspect = Float(view.drawableSize.width / view.drawableSize.height)

        // Build uniforms
        var uniforms = Uniforms()
        uniforms.viewMatrix = camera.viewMatrix()
        uniforms.projectionMatrix = camera.projectionMatrix(aspect: aspect)
        uniforms.time = Float(CACurrentMediaTime())

        // Depth stencil state (enable depth test)
        let depthDesc = MTLDepthStencilDescriptor()
        depthDesc.depthCompareFunction = .less
        depthDesc.isDepthWriteEnabled = true
        let depthState = device.makeDepthStencilState(descriptor: depthDesc)!

        let encoder = cmdBuf.makeRenderCommandEncoder(descriptor: rpd)!
        encoder.setDepthStencilState(depthState)
        encoder.setCullMode(.none)  // Tiles use two-sided rendering

        // Draw ground plane (uses tile body pipeline with identity instance)
        do {
            encoder.setRenderPipelineState(tileBodyPipeline)
            encoder.setVertexBuffer(groundVertexBuffer, offset: 0, index: 0)
            encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
            var groundInst = InstanceData()
            groundInst.modelMatrix = matrix_identity_float4x4
            groundInst.normalMatrix = float3x3(1)
            groundInst.tintColor = SIMD4<Float>(1, 1, 1, 1)
            encoder.setVertexBytes(&groundInst, length: MemoryLayout<InstanceData>.stride, index: 2)
            encoder.drawIndexedPrimitives(
                type: .triangle, indexCount: 6, indexType: .uint32,
                indexBuffer: groundIndexBuffer, indexBufferOffset: 0, instanceCount: 1
            )
        }

        // Build instances for tile(s)
        let instances = buildInstances()

        if frameCount == 1 {
            let m = (orientation == .flat) ? flatMesh! : standingMesh!
            NSLog("[Renderer] Frame 1: %d instances, body=%d idx, face=%d idx, shadow=%d idx", instances.count, m.indexCount, m.faceIndexCount, m.shadowIndexCount)
            NSLog("[Renderer] Camera eye=(%f,%f,%f) dist=%f pitch=%f", camera.eye.x, camera.eye.y, camera.eye.z, camera.distance, camera.pitch)
            NSLog("[Renderer] Atlas: %@", tileAtlas.texture != nil ? "loaded" : "MISSING")
            if let inst = instances.first {
                NSLog("[Renderer] Inst0 atlasUV=(%f,%f,%f,%f) showFace=%d", inst.atlasUV.x, inst.atlasUV.y, inst.atlasUV.z, inst.atlasUV.w, inst.showFace)
            }
        }

        let instanceBuffer = device.makeBuffer(
            bytes: instances,
            length: instances.count * MemoryLayout<InstanceData>.stride,
            options: .storageModeShared
        )!

        let mesh = (orientation == .flat) ? flatMesh! : standingMesh!

        // Shadow pass
        encoder.setRenderPipelineState(shadowPipeline)
        encoder.setVertexBuffer(mesh.shadowVertexBuffer, offset: 0, index: 0)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setVertexBuffer(instanceBuffer, offset: 0, index: 2)
        encoder.drawIndexedPrimitives(
            type: .triangle,
            indexCount: mesh.shadowIndexCount,
            indexType: .uint32,
            indexBuffer: mesh.shadowIndexBuffer,
            indexBufferOffset: 0,
            instanceCount: instances.count
        )

        // Tile body pass
        encoder.setRenderPipelineState(tileBodyPipeline)
        encoder.setVertexBuffer(mesh.vertexBuffer, offset: 0, index: 0)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setVertexBuffer(instanceBuffer, offset: 0, index: 2)
        encoder.drawIndexedPrimitives(
            type: .triangle,
            indexCount: mesh.indexCount,
            indexType: .uint32,
            indexBuffer: mesh.indexBuffer,
            indexBufferOffset: 0,
            instanceCount: instances.count
        )

        // Wireframe overlay
        if showWireframe {
            encoder.setRenderPipelineState(wireframePipeline)
            encoder.setTriangleFillMode(.lines)
            encoder.drawIndexedPrimitives(
                type: .triangle,
                indexCount: mesh.indexCount,
                indexType: .uint32,
                indexBuffer: mesh.indexBuffer,
                indexBufferOffset: 0,
                instanceCount: instances.count
            )
            encoder.setTriangleFillMode(.fill)
        }

        // Face texture pass
        if showFaceTexture && orientation != .back {
            if let atlasTex = tileAtlas.texture {
                encoder.setRenderPipelineState(texturedQuadPipeline)
                encoder.setVertexBuffer(mesh.faceVertexBuffer, offset: 0, index: 0)
                encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
                encoder.setVertexBuffer(instanceBuffer, offset: 0, index: 2)
                encoder.setFragmentTexture(atlasTex, index: 0)
                encoder.setFragmentSamplerState(samplerState, index: 0)
                encoder.drawIndexedPrimitives(
                    type: .triangle,
                    indexCount: mesh.faceIndexCount,
                    indexType: .uint32,
                    indexBuffer: mesh.faceIndexBuffer,
                    indexBufferOffset: 0,
                    instanceCount: instances.count
                )
            }
        }

        encoder.endEncoding()
        cmdBuf.present(drawable)
        cmdBuf.commit()
    }

    // MARK: - Instance building

    private func buildInstances() -> [InstanceData] {
        var instances = [InstanceData]()

        // Sample tiles for preview: Bamboo 1, Characters 5, Dots 9, East Wind, Red Dragon
        let sampleTiles: [(suit: Int, rank: Int)] = [
            (0, 1), (1, 5), (2, 9), (3, 1), (4, 1)
        ]

        let count = min(tileCount, sampleTiles.count)
        let spacing: Float = TileMeshFactory.TILE_W + 0.15

        for i in 0..<count {
            let xOffset = (Float(i) - Float(count - 1) * 0.5) * spacing

            var modelMatrix: float4x4

            switch orientation {
            case .standing:
                // Standing: body center at (x, TILE_H/2, 0)
                modelMatrix = float4x4.translation(SIMD3<Float>(xOffset, TileMeshFactory.TILE_H * 0.5, 0))

            case .flat:
                // Flat: body center at (x, TILE_D/2, 0)
                modelMatrix = float4x4.translation(SIMD3<Float>(xOffset, TileMeshFactory.TILE_D * 0.5, 0))

            case .tilted:
                // Tilted: -65 degrees around X, lifted
                let tiltDeg: Float = -65
                let tiltRad = tiltDeg * .pi / 180
                let lift = (TileMeshFactory.TILE_D * 0.5) * sinf(abs(tiltRad))
                let translate = float4x4.translation(SIMD3<Float>(xOffset, lift, 0))
                let tilt = float4x4.rotationX(tiltRad)
                let bodyOffset = float4x4.translation(SIMD3<Float>(0, TileMeshFactory.TILE_H * 0.5, 0))
                modelMatrix = translate * tilt * bodyOffset

            case .back:
                // Same as standing but no face texture
                modelMatrix = float4x4.translation(SIMD3<Float>(xOffset, TileMeshFactory.TILE_H * 0.5, 0))
            }

            let normalMatrix = modelMatrix.upperLeft3x3

            var inst = InstanceData()
            inst.modelMatrix = modelMatrix
            inst.normalMatrix = normalMatrix
            inst.tintColor = SIMD4<Float>(1, 1, 1, 1)
            inst.atlasUV = tileAtlas.getUV(suitIndex: sampleTiles[i].suit, rank: sampleTiles[i].rank)
            inst.showFace = (orientation != .back) ? 1 : 0
            instances.append(inst)
        }

        return instances
    }
}
