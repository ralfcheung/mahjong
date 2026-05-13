import Metal
import AppKit
import simd

/// Loads tile PNGs and composites them into a 7x7 atlas MTLTexture.
/// Matches the layout from TileTextureAtlas.cpp.
class TileAtlas {
    static let CELL_SIZE = 256
    static let GRID_COLS = 7
    static let GRID_ROWS = 7
    static let TILE_MARGIN = 30

    private(set) var texture: MTLTexture?
    private var uvMap: [UInt16: SIMD4<Float>] = [:]  // key -> (u, v, w, h) in 0..1

    struct TileEntry {
        let suit: String   // "bamboo", "characters", "dots", "winds", "dragons", "flowers", "seasons"
        let rank: Int
        let filename: String
    }

    /// Generate atlas from tile PNGs in the given assets directory.
    func generate(device: MTLDevice, assetsPath: String, tileVersion: String = "live") {
        let entries = buildTileList(version: tileVersion)
        let atlasW = TileAtlas.GRID_COLS * TileAtlas.CELL_SIZE
        let atlasH = TileAtlas.GRID_ROWS * TileAtlas.CELL_SIZE

        // Create RGBA8 pixel buffer
        var pixels = [UInt8](repeating: 0, count: atlasW * atlasH * 4)

        var col = 0, row = 0
        for (i, entry) in entries.enumerated() {
            let px = col * TileAtlas.CELL_SIZE
            let py = row * TileAtlas.CELL_SIZE

            let path = "\(assetsPath)/tiles/\(entry.filename)"
            if let image = NSImage(contentsOfFile: path) {
                if i < 3 { NSLog("[TileAtlas] Loaded: %@ (%dx%d)", entry.filename, Int(image.size.width), Int(image.size.height)) }
                let isBonus = entry.suit == "flowers" || entry.suit == "seasons"
                let margin = isBonus ? 4 : TileAtlas.TILE_MARGIN
                blitImage(image, into: &pixels, atlasWidth: atlasW,
                          destX: px + margin, destY: py + margin,
                          destW: TileAtlas.CELL_SIZE - margin * 2,
                          destH: TileAtlas.CELL_SIZE - margin * 2)
            }

            // Store UV rect (normalized 0..1)
            let key = tileKey(entry: entry, index: i)
            let u = Float(px) / Float(atlasW)
            let v = Float(py) / Float(atlasH)
            let w = Float(TileAtlas.CELL_SIZE) / Float(atlasW)
            let h = Float(TileAtlas.CELL_SIZE) / Float(atlasH)
            uvMap[key] = SIMD4<Float>(u, v, w, h)

            col += 1
            if col >= TileAtlas.GRID_COLS { col = 0; row += 1 }
        }

        // Create MTLTexture
        let desc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm,
            width: atlasW, height: atlasH,
            mipmapped: true
        )
        desc.usage = MTLTextureUsage.shaderRead
        let tex = device.makeTexture(descriptor: desc)!
        tex.replace(region: MTLRegionMake2D(0, 0, atlasW, atlasH),
                    mipmapLevel: 0,
                    withBytes: pixels,
                    bytesPerRow: atlasW * 4)

        // Generate mipmaps
        if let cmdQueue = device.makeCommandQueue(),
           let cmdBuf = cmdQueue.makeCommandBuffer(),
           let blit = cmdBuf.makeBlitCommandEncoder() {
            blit.generateMipmaps(for: tex)
            blit.endEncoding()
            cmdBuf.commit()
            cmdBuf.waitUntilCompleted()
        }

        texture = tex
    }

    /// Get atlas UV rect for a tile (suit index 0-6, rank 1-based).
    /// Returns (u, v, w, h) in normalized coordinates.
    func getUV(suitIndex: Int, rank: Int) -> SIMD4<Float> {
        // Compute linear index in the tile list
        let index: Int
        switch suitIndex {
        case 0: index = rank - 1           // bamboo 1-9
        case 1: index = 9 + rank - 1       // characters 1-9
        case 2: index = 18 + rank - 1      // dots 1-9
        case 3: index = 27 + rank - 1      // winds 1-4
        case 4: index = 31 + rank - 1      // dragons 1-3
        case 5: index = 34 + rank - 1      // flowers 1-4
        case 6: index = 38 + rank - 1      // seasons 1-4
        default: return .zero
        }

        let col = index % TileAtlas.GRID_COLS
        let row = index / TileAtlas.GRID_COLS
        let atlasW = Float(TileAtlas.GRID_COLS * TileAtlas.CELL_SIZE)
        let atlasH = Float(TileAtlas.GRID_ROWS * TileAtlas.CELL_SIZE)
        return SIMD4<Float>(
            Float(col * TileAtlas.CELL_SIZE) / atlasW,
            Float(row * TileAtlas.CELL_SIZE) / atlasH,
            Float(TileAtlas.CELL_SIZE) / atlasW,
            Float(TileAtlas.CELL_SIZE) / atlasH
        )
    }

    // MARK: - Private

    private func tileKey(entry: TileEntry, index: Int) -> UInt16 {
        return UInt16(index)
    }

    private func buildTileList(version: String) -> [TileEntry] {
        var list = [TileEntry]()
        // Bamboo 1-9
        for r in 1...9 { list.append(TileEntry(suit: "bamboo", rank: r, filename: "bamboo/\(version)/Sou\(r).png")) }
        // Characters 1-9
        for r in 1...9 { list.append(TileEntry(suit: "characters", rank: r, filename: "characters/\(version)/Man\(r).png")) }
        // Dots 1-9
        for r in 1...9 { list.append(TileEntry(suit: "dots", rank: r, filename: "dots/\(version)/Pin\(r).png")) }
        // Winds 1-4
        let windNames = ["Ton", "Nan", "Shaa", "Pei"]
        for r in 1...4 { list.append(TileEntry(suit: "winds", rank: r, filename: "winds/\(version)/\(windNames[r-1]).png")) }
        // Dragons 1-3
        let dragonNames = ["Chun", "Hatsu", "Haku"]
        for r in 1...3 { list.append(TileEntry(suit: "dragons", rank: r, filename: "dragons/\(version)/\(dragonNames[r-1]).png")) }
        // Flowers 1-4
        for r in 1...4 { list.append(TileEntry(suit: "flowers", rank: r, filename: "flowers/\(version)/Flower\(r).png")) }
        // Seasons 1-4
        for r in 1...4 { list.append(TileEntry(suit: "seasons", rank: r, filename: "seasons/\(version)/Season\(r).png")) }
        return list
    }

    private func blitImage(_ image: NSImage, into pixels: inout [UInt8], atlasWidth: Int,
                           destX: Int, destY: Int, destW: Int, destH: Int) {
        guard let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return }

        // Render image to RGBA buffer at destination size
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        var imgPixels = [UInt8](repeating: 0, count: destW * destH * 4)
        guard let ctx = CGContext(data: &imgPixels, width: destW, height: destH,
                                  bitsPerComponent: 8, bytesPerRow: destW * 4,
                                  space: colorSpace,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        ctx.draw(cgImage, in: CGRect(x: 0, y: 0, width: destW, height: destH))

        // Copy into atlas (flip Y since CGContext is bottom-up)
        for y in 0..<destH {
            let srcRow = destH - 1 - y
            let srcOffset = srcRow * destW * 4
            let dstOffset = ((destY + y) * atlasWidth + destX) * 4
            for x in 0..<destW {
                let si = srcOffset + x * 4
                let di = dstOffset + x * 4
                pixels[di]     = imgPixels[si]
                pixels[di + 1] = imgPixels[si + 1]
                pixels[di + 2] = imgPixels[si + 2]
                pixels[di + 3] = imgPixels[si + 3]
            }
        }
    }
}
