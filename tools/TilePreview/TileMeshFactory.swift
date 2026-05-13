import Metal
import Foundation
import simd

/// Pre-bakes tile body mesh as MTLBuffers.
/// Replicates geometry from TileRenderer::drawTileBody() in C++.
///
/// Tile dimensions (world units):
///   TILE_W = 0.72  (width, X)
///   TILE_H = 0.98  (height, Y)
///   TILE_D = 0.50  (depth, Z)
///
/// Two-tone: white front (55%) + green back (45%), split along Z axis (backDir = -3).
/// Corner radius: 0.08, with 5 arc segments per quarter-circle.
struct TileMeshFactory {

    static let TILE_W: Float = 0.72
    static let TILE_H: Float = 0.98
    static let TILE_D: Float = 0.50
    static let ROUND_R: Float = 0.08
    static let ROUND_N: Int = 5

    // Colors matching TileRenderer.cpp
    static let frontColor = SIMD4<Float>(240.0/255, 238.0/255, 234.0/255, 1.0)
    static let backColor  = SIMD4<Float>(45.0/255, 110.0/255, 90.0/255, 1.0)
    static let seamColor  = SIMD4<Float>(30.0/255, 70.0/255, 50.0/255, 1.0)
    static let greenFrac: Float = 0.45

    struct TileMesh {
        let vertexBuffer: MTLBuffer
        let indexBuffer: MTLBuffer
        let indexCount: Int
        // Face decal quad (separate mesh)
        let faceVertexBuffer: MTLBuffer
        let faceIndexBuffer: MTLBuffer
        let faceIndexCount: Int
        // Shadow quad
        let shadowVertexBuffer: MTLBuffer
        let shadowIndexBuffer: MTLBuffer
        let shadowIndexCount: Int
    }

    /// Build a standing tile mesh (body centered at origin, extends [-hx..hx, -hy..hy, -hz..hz]).
    /// The mesh is for a tile with `size` and two-tone split.
    /// backDir: -3 means back is -Z side, backFrac: 0.45
    static func buildStandingTileMesh(device: MTLDevice) -> TileMesh {
        let size = SIMD3<Float>(TILE_W, TILE_H, TILE_D)
        return buildTileMesh(device: device, size: size, backDir: -3, backFrac: greenFrac)
    }

    /// Build a flat tile mesh (lying on table, depth/height swapped).
    static func buildFlatTileMesh(device: MTLDevice) -> TileMesh {
        let size = SIMD3<Float>(TILE_W, TILE_D, TILE_H)
        return buildTileMesh(device: device, size: size, backDir: -2, backFrac: greenFrac)
    }

    // MARK: - Core mesh builder

    private static func buildTileMesh(device: MTLDevice, size: SIMD3<Float>,
                                       backDir: Int, backFrac: Float) -> TileMesh {
        var vertices: [VertexIn] = []
        var indices: [UInt32] = []

        let hx = size.x * 0.5
        let hy = size.y * 0.5
        let hz = size.z * 0.5
        let r = min(ROUND_R, min(hx, min(hy, hz)))

        let ix = hx - r
        let iy = hy - r
        let iz = hz - r

        // Two-tone setup (matches C++ logic)
        let twoTone = backDir != 0
        let sa = twoTone ? (abs(backDir) - 1) : -1  // split axis: 0=X, 1=Y, 2=Z
        let sd: Float = backDir > 0 ? 1.0 : -1.0
        let iExts: [Float] = [ix, iy, iz]
        var sc: Float = 0
        if twoTone {
            let he = iExts[sa]
            sc = sd * he - sd * 2.0 * he * backFrac
        }

        let isGreen: (Float) -> Bool = { coord in
            twoTone && (sd * coord > sd * sc)
        }

        // Precompute arc tables
        var cs = [Float](repeating: 0, count: ROUND_N + 1)
        var sn = [Float](repeating: 0, count: ROUND_N + 1)
        for i in 0...ROUND_N {
            let a = Float(i) / Float(ROUND_N) * .pi * 0.5
            cs[i] = cosf(a)
            sn[i] = sinf(a)
        }

        // Helper: add a vertex and return its index
        func addVertex(_ pos: SIMD3<Float>, _ normal: SIMD3<Float>, _ color: SIMD4<Float>) -> UInt32 {
            let idx = UInt32(vertices.count)
            var v = VertexIn()
            v.position = pos
            v.normal = normal
            v.color = color
            v.texCoord = SIMD2<Float>(0, 0)
            vertices.append(v)
            return idx
        }

        // Vertex color computation matching C++ vc() lambda
        // Note: We bake the BASE color (before normalShade), the shader applies normalShade
        func vertexColor(normal: SIMD3<Float>, green: Bool) -> SIMD4<Float> {
            var base = green ? backColor : frontColor

            // Fake AO for white face (matches C++)
            if !green {
                var grad: Float = 1.0
                if normal.y < 0 { grad -= 0.15 * abs(normal.y) }
                if abs(normal.x) > 0.8 { grad -= 0.1 }
                if grad < 1.0 {
                    base.x *= grad
                    base.y *= grad
                    base.z *= grad
                }
            }

            return base
        }

        // Helper to add a quad (two triangles) with consistent winding
        func addQuad(_ v0: UInt32, _ v1: UInt32, _ v2: UInt32, _ v3: UInt32) {
            indices.append(contentsOf: [v0, v1, v2, v0, v2, v3])
        }

        // Determine face extents
        let feYx = ix, feYz = iz  // Top/Bottom
        let feZx = ix, feZy = iy  // Front/Back
        let feXy = iy, feXz = iz  // Left/Right

        // rz: true when hz <= hy (standing tile), meaning Z is the "short" axis
        // and rounded caps go on the Z ends
        let rz = hz <= hy

        // ---------- 6 main faces ----------

        // Helper for flat face with optional two-tone split
        func addFlatFace(axis: Int, level: Float, isPositive: Bool,
                         extU: Float, extV: Float, uAxis: Int, vAxis: Int) {
            let n: SIMD3<Float> = {
                var v = SIMD3<Float>(0, 0, 0)
                v[axis] = isPositive ? 1.0 : -1.0
                return v
            }()

            // Check if this face needs two-tone split
            let needsSplit = twoTone && (sa != axis) && {
                // The split axis runs across this face
                return sa == uAxis || sa == vAxis
            }()

            if needsSplit && (sa == vAxis || sa == uAxis) {
                // Split along the split axis
                let splitOnU = (sa == uAxis)
                let coordForGreen: (Float, Float) -> Bool = { u, v in
                    if splitOnU { return isGreen(u) }
                    return isGreen(v)
                }

                // Lower half: from -ext to sc
                let splitCoord = sc
                let green1 = coordForGreen(splitOnU ? -extU : 0, splitOnU ? 0 : -extV)
                let green2 = coordForGreen(splitOnU ? extU : 0, splitOnU ? 0 : extV)

                if splitOnU {
                    // Split along U axis
                    let c1 = vertexColor(normal: n, green: green1)
                    var p0 = SIMD3<Float>(0, 0, 0); p0[axis] = level; p0[uAxis] = -extU; p0[vAxis] = -extV
                    var p1 = SIMD3<Float>(0, 0, 0); p1[axis] = level; p1[uAxis] = splitCoord; p1[vAxis] = -extV
                    var p2 = SIMD3<Float>(0, 0, 0); p2[axis] = level; p2[uAxis] = splitCoord; p2[vAxis] = extV
                    var p3 = SIMD3<Float>(0, 0, 0); p3[axis] = level; p3[uAxis] = -extU; p3[vAxis] = extV
                    let i0 = addVertex(p0, n, c1)
                    let i1 = addVertex(p1, n, c1)
                    let i2 = addVertex(p2, n, c1)
                    let i3 = addVertex(p3, n, c1)
                    if isPositive {
                        addQuad(i0, i3, i2, i1)
                    } else {
                        addQuad(i0, i1, i2, i3)
                    }

                    let c2 = vertexColor(normal: n, green: green2)
                    var q0 = SIMD3<Float>(0, 0, 0); q0[axis] = level; q0[uAxis] = splitCoord; q0[vAxis] = -extV
                    var q1 = SIMD3<Float>(0, 0, 0); q1[axis] = level; q1[uAxis] = extU; q1[vAxis] = -extV
                    var q2 = SIMD3<Float>(0, 0, 0); q2[axis] = level; q2[uAxis] = extU; q2[vAxis] = extV
                    var q3 = SIMD3<Float>(0, 0, 0); q3[axis] = level; q3[uAxis] = splitCoord; q3[vAxis] = extV
                    let j0 = addVertex(q0, n, c2)
                    let j1 = addVertex(q1, n, c2)
                    let j2 = addVertex(q2, n, c2)
                    let j3 = addVertex(q3, n, c2)
                    if isPositive {
                        addQuad(j0, j3, j2, j1)
                    } else {
                        addQuad(j0, j1, j2, j3)
                    }
                } else {
                    // Split along V axis
                    let c1 = vertexColor(normal: n, green: green1)
                    var p0 = SIMD3<Float>(0, 0, 0); p0[axis] = level; p0[uAxis] = -extU; p0[vAxis] = -extV
                    var p1 = SIMD3<Float>(0, 0, 0); p1[axis] = level; p1[uAxis] = extU; p1[vAxis] = -extV
                    var p2 = SIMD3<Float>(0, 0, 0); p2[axis] = level; p2[uAxis] = extU; p2[vAxis] = splitCoord
                    var p3 = SIMD3<Float>(0, 0, 0); p3[axis] = level; p3[uAxis] = -extU; p3[vAxis] = splitCoord
                    let i0 = addVertex(p0, n, c1)
                    let i1 = addVertex(p1, n, c1)
                    let i2 = addVertex(p2, n, c1)
                    let i3 = addVertex(p3, n, c1)
                    if isPositive {
                        addQuad(i0, i3, i2, i1)
                    } else {
                        addQuad(i0, i1, i2, i3)
                    }

                    let c2 = vertexColor(normal: n, green: green2)
                    var q0 = SIMD3<Float>(0, 0, 0); q0[axis] = level; q0[uAxis] = -extU; q0[vAxis] = splitCoord
                    var q1 = SIMD3<Float>(0, 0, 0); q1[axis] = level; q1[uAxis] = extU; q1[vAxis] = splitCoord
                    var q2 = SIMD3<Float>(0, 0, 0); q2[axis] = level; q2[uAxis] = extU; q2[vAxis] = extV
                    var q3 = SIMD3<Float>(0, 0, 0); q3[axis] = level; q3[uAxis] = -extU; q3[vAxis] = extV
                    let j0 = addVertex(q0, n, c2)
                    let j1 = addVertex(q1, n, c2)
                    let j2 = addVertex(q2, n, c2)
                    let j3 = addVertex(q3, n, c2)
                    if isPositive {
                        addQuad(j0, j3, j2, j1)
                    } else {
                        addQuad(j0, j1, j2, j3)
                    }
                }
            } else {
                // Single-color face
                let coordOnSplitAxis: Float = {
                    if sa == axis { return isPositive ? iExts[sa] : -iExts[sa] }
                    return 0
                }()
                let green = twoTone && isGreen(coordOnSplitAxis)
                let c = vertexColor(normal: n, green: green)

                var p0 = SIMD3<Float>(0, 0, 0); p0[axis] = level; p0[uAxis] = -extU; p0[vAxis] = -extV
                var p1 = SIMD3<Float>(0, 0, 0); p1[axis] = level; p1[uAxis] = extU; p1[vAxis] = -extV
                var p2 = SIMD3<Float>(0, 0, 0); p2[axis] = level; p2[uAxis] = extU; p2[vAxis] = extV
                var p3 = SIMD3<Float>(0, 0, 0); p3[axis] = level; p3[uAxis] = -extU; p3[vAxis] = extV
                let i0 = addVertex(p0, n, c)
                let i1 = addVertex(p1, n, c)
                let i2 = addVertex(p2, n, c)
                let i3 = addVertex(p3, n, c)
                if isPositive {
                    addQuad(i0, i3, i2, i1)
                } else {
                    addQuad(i0, i1, i2, i3)
                }
            }
        }

        // Top (+Y) and Bottom (-Y)
        if rz {
            // When rz, rounded caps are on Z ends. Top/bottom are flat.
            addFlatFace(axis: 1, level: hy, isPositive: true, extU: feYx, extV: feYz, uAxis: 0, vAxis: 2)
            addFlatFace(axis: 1, level: -hy, isPositive: false, extU: feYx, extV: feYz, uAxis: 0, vAxis: 2)
        } else {
            // Rounded caps on Y ends
            addRoundedCap(axis: 1, level: hy, isTop: true,
                          ix: ix, iy: iy, iz: iz, r: r,
                          cs: cs, sn: sn, twoTone: twoTone, sa: sa, sd: sd, sc: sc,
                          isGreen: isGreen, vertexColor: vertexColor,
                          addVertex: addVertex, indices: &indices)
            addRoundedCap(axis: 1, level: -hy, isTop: false,
                          ix: ix, iy: iy, iz: iz, r: r,
                          cs: cs, sn: sn, twoTone: twoTone, sa: sa, sd: sd, sc: sc,
                          isGreen: isGreen, vertexColor: vertexColor,
                          addVertex: addVertex, indices: &indices)
        }

        // Front (+Z) and Back (-Z)
        if rz {
            // Rounded caps on Z ends
            addRoundedCap(axis: 2, level: hz, isTop: true,
                          ix: ix, iy: iy, iz: iz, r: r,
                          cs: cs, sn: sn, twoTone: twoTone, sa: sa, sd: sd, sc: sc,
                          isGreen: isGreen, vertexColor: vertexColor,
                          addVertex: addVertex, indices: &indices)
            addRoundedCap(axis: 2, level: -hz, isTop: false,
                          ix: ix, iy: iy, iz: iz, r: r,
                          cs: cs, sn: sn, twoTone: twoTone, sa: sa, sd: sd, sc: sc,
                          isGreen: isGreen, vertexColor: vertexColor,
                          addVertex: addVertex, indices: &indices)
        } else {
            addFlatFace(axis: 2, level: hz, isPositive: true, extU: feZx, extV: feZy, uAxis: 0, vAxis: 1)
            addFlatFace(axis: 2, level: -hz, isPositive: false, extU: feZx, extV: feZy, uAxis: 0, vAxis: 1)
        }

        // Right (+X) and Left (-X) - always flat faces
        addFlatFace(axis: 0, level: hx, isPositive: true, extU: feXz, extV: feXy, uAxis: 2, vAxis: 1)
        addFlatFace(axis: 0, level: -hx, isPositive: false, extU: feXz, extV: feXy, uAxis: 2, vAxis: 1)

        // ---------- 4 edge strips ----------
        addEdgeStrips(vertices: &vertices, indices: &indices,
                      ix: ix, iy: iy, iz: iz, r: r, rz: rz,
                      cs: cs, sn: sn, hy: hy, hz: hz,
                      twoTone: twoTone, sa: sa, sd: sd, sc: sc,
                      isGreen: isGreen, vertexColor: vertexColor)

        // ---------- Rounded cap edge strips + corner fans ----------
        // The rounded caps also need the margin strips and corner fans
        // These are generated inside addRoundedCap

        // Debug: print mesh stats
        NSLog("[TileMeshFactory] Body mesh: %d vertices, %d indices (%d triangles)", vertices.count, indices.count, indices.count / 3)
        if let first = vertices.first {
            NSLog("[TileMeshFactory] First vertex: pos=(%f,%f,%f) normal=(%f,%f,%f)", first.position.x, first.position.y, first.position.z, first.normal.x, first.normal.y, first.normal.z)
        }

        // Build Metal buffers
        let vertexBuffer = device.makeBuffer(bytes: vertices,
                                              length: vertices.count * MemoryLayout<VertexIn>.stride,
                                              options: .storageModeShared)!
        let indexBuffer = device.makeBuffer(bytes: indices,
                                             length: indices.count * MemoryLayout<UInt32>.stride,
                                             options: .storageModeShared)!

        // Face decal quad
        let (faceVerts, faceIdxs) = buildFaceQuad(standing: size.y > size.z)
        let faceVB = device.makeBuffer(bytes: faceVerts,
                                        length: faceVerts.count * MemoryLayout<VertexIn>.stride,
                                        options: .storageModeShared)!
        let faceIB = device.makeBuffer(bytes: faceIdxs,
                                        length: faceIdxs.count * MemoryLayout<UInt32>.stride,
                                        options: .storageModeShared)!

        // Shadow quad
        let (shadowVerts, shadowIdxs) = buildShadowQuad(width: size.x, depth: size.y > size.z ? size.z : size.y)
        let shadowVB = device.makeBuffer(bytes: shadowVerts,
                                          length: shadowVerts.count * MemoryLayout<VertexIn>.stride,
                                          options: .storageModeShared)!
        let shadowIB = device.makeBuffer(bytes: shadowIdxs,
                                          length: shadowIdxs.count * MemoryLayout<UInt32>.stride,
                                          options: .storageModeShared)!

        return TileMesh(
            vertexBuffer: vertexBuffer, indexBuffer: indexBuffer,
            indexCount: indices.count,
            faceVertexBuffer: faceVB, faceIndexBuffer: faceIB,
            faceIndexCount: faceIdxs.count,
            shadowVertexBuffer: shadowVB, shadowIndexBuffer: shadowIB,
            shadowIndexCount: shadowIdxs.count
        )
    }

    // MARK: - Rounded cap (top/bottom of a face)
    // Generates: center quad, 4 margin strips, 4 corner fans

    private static func addRoundedCap(
        axis: Int, level: Float, isTop: Bool,
        ix: Float, iy: Float, iz: Float, r: Float,
        cs: [Float], sn: [Float],
        twoTone: Bool, sa: Int, sd: Float, sc: Float,
        isGreen: (Float) -> Bool,
        vertexColor: (SIMD3<Float>, Bool) -> SIMD4<Float>,
        addVertex: (SIMD3<Float>, SIMD3<Float>, SIMD4<Float>) -> UInt32,
        indices: inout [UInt32]
    ) {
        var n = SIMD3<Float>(0, 0, 0)
        n[axis] = isTop ? 1.0 : -1.0
        let capCoord = isTop ? level : -level
        let capGreen = twoTone && (sa == axis) ? isGreen(capCoord) : false

        let uAx = (axis == 1) ? 0 : 0
        let vAx = (axis == 1) ? 2 : 1
        let uExt = ix
        let vExt = (axis == 1) ? iz : iy

        let c = vertexColor(n, capGreen)

        // Center quad
        var c0 = SIMD3<Float>(0, 0, 0); c0[axis] = level; c0[uAx] = -uExt; c0[vAx] = -vExt
        var c1 = SIMD3<Float>(0, 0, 0); c1[axis] = level; c1[uAx] = uExt; c1[vAx] = -vExt
        var c2 = SIMD3<Float>(0, 0, 0); c2[axis] = level; c2[uAx] = uExt; c2[vAx] = vExt
        var c3 = SIMD3<Float>(0, 0, 0); c3[axis] = level; c3[uAx] = -uExt; c3[vAx] = vExt

        let i0 = addVertex(c0, n, c)
        let i1 = addVertex(c1, n, c)
        let i2 = addVertex(c2, n, c)
        let i3 = addVertex(c3, n, c)
        if isTop {
            indices.append(contentsOf: [i0, i2, i1, i0, i3, i2])
        } else {
            indices.append(contentsOf: [i0, i1, i2, i0, i2, i3])
        }

        // 4 margin strips
        let marginQuads: [(Float, Float, Float, Float)] = [
            (-uExt, vExt, uExt, vExt + r),   // top
            (-uExt, -vExt - r, uExt, -vExt), // bottom
            (uExt, -vExt, uExt + r, vExt),   // right
            (-uExt - r, -vExt, -uExt, vExt), // left
        ]
        for (u0, v0, u1, v1) in marginQuads {
            var q0 = SIMD3<Float>(0, 0, 0); q0[axis] = level; q0[uAx] = u0; q0[vAx] = v0
            var q1 = SIMD3<Float>(0, 0, 0); q1[axis] = level; q1[uAx] = u1; q1[vAx] = v0
            var q2 = SIMD3<Float>(0, 0, 0); q2[axis] = level; q2[uAx] = u1; q2[vAx] = v1
            var q3 = SIMD3<Float>(0, 0, 0); q3[axis] = level; q3[uAx] = u0; q3[vAx] = v1
            let j0 = addVertex(q0, n, c)
            let j1 = addVertex(q1, n, c)
            let j2 = addVertex(q2, n, c)
            let j3 = addVertex(q3, n, c)
            if isTop {
                indices.append(contentsOf: [j0, j2, j1, j0, j3, j2])
            } else {
                indices.append(contentsOf: [j0, j1, j2, j0, j2, j3])
            }
        }

        // 4 corner fans
        let corners: [(Float, Float, Float)] = [
            (uExt, vExt, 0),
            (-uExt, vExt, .pi * 0.5),
            (-uExt, -vExt, .pi),
            (uExt, -vExt, .pi * 1.5),
        ]
        for (cx, cy, startAng) in corners {
            var center = SIMD3<Float>(0, 0, 0)
            center[axis] = level; center[uAx] = cx; center[vAx] = cy
            let ci = addVertex(center, n, c)

            for seg in 0..<ROUND_N {
                let a1 = startAng + Float(seg) / Float(ROUND_N) * .pi * 0.5
                let a2 = startAng + Float(seg + 1) / Float(ROUND_N) * .pi * 0.5

                var p1 = SIMD3<Float>(0, 0, 0)
                p1[axis] = level; p1[uAx] = cx + cosf(a1) * r; p1[vAx] = cy + sinf(a1) * r
                var p2 = SIMD3<Float>(0, 0, 0)
                p2[axis] = level; p2[uAx] = cx + cosf(a2) * r; p2[vAx] = cy + sinf(a2) * r

                let pi1 = addVertex(p1, n, c)
                let pi2 = addVertex(p2, n, c)

                if isTop {
                    indices.append(contentsOf: [ci, pi2, pi1])
                } else {
                    indices.append(contentsOf: [ci, pi1, pi2])
                }
            }
        }
    }

    // MARK: - Edge strips (4 rounded edges)

    private static func addEdgeStrips(
        vertices: inout [VertexIn], indices: inout [UInt32],
        ix: Float, iy: Float, iz: Float, r: Float, rz: Bool,
        cs: [Float], sn: [Float], hy: Float, hz: Float,
        twoTone: Bool, sa: Int, sd: Float, sc: Float,
        isGreen: (Float) -> Bool,
        vertexColor: (SIMD3<Float>, Bool) -> SIMD4<Float>
    ) {
        struct EdgeDef {
            let axis: Int
            let lo: Float, hi: Float
            let cx: Float, cy: Float, cz: Float
            let d1x: Float, d1y: Float, d1z: Float
            let d2x: Float, d2y: Float, d2z: Float
        }

        let edges: [EdgeDef]
        if rz {
            edges = [
                EdgeDef(axis: 2, lo: -hz, hi: hz, cx: ix, cy: iy, cz: 0,
                        d1x: 0, d1y: 1, d1z: 0, d2x: 1, d2y: 0, d2z: 0),
                EdgeDef(axis: 2, lo: -hz, hi: hz, cx: -ix, cy: iy, cz: 0,
                        d1x: 0, d1y: 1, d1z: 0, d2x: -1, d2y: 0, d2z: 0),
                EdgeDef(axis: 2, lo: -hz, hi: hz, cx: ix, cy: -iy, cz: 0,
                        d1x: 1, d1y: 0, d1z: 0, d2x: 0, d2y: -1, d2z: 0),
                EdgeDef(axis: 2, lo: -hz, hi: hz, cx: -ix, cy: -iy, cz: 0,
                        d1x: 0, d1y: -1, d1z: 0, d2x: -1, d2y: 0, d2z: 0),
            ]
        } else {
            edges = [
                EdgeDef(axis: 1, lo: -hy, hi: hy, cx: ix, cy: 0, cz: iz,
                        d1x: 0, d1y: 0, d1z: 1, d2x: 1, d2y: 0, d2z: 0),
                EdgeDef(axis: 1, lo: -hy, hi: hy, cx: -ix, cy: 0, cz: iz,
                        d1x: 0, d1y: 0, d1z: 1, d2x: -1, d2y: 0, d2z: 0),
                EdgeDef(axis: 1, lo: -hy, hi: hy, cx: ix, cy: 0, cz: -iz,
                        d1x: 1, d1y: 0, d1z: 0, d2x: 0, d2y: 0, d2z: -1),
                EdgeDef(axis: 1, lo: -hy, hi: hy, cx: -ix, cy: 0, cz: -iz,
                        d1x: 0, d1y: 0, d1z: -1, d2x: -1, d2y: 0, d2z: 0),
            ]
        }

        func addV(_ pos: SIMD3<Float>, _ normal: SIMD3<Float>, _ color: SIMD4<Float>) -> UInt32 {
            let idx = UInt32(vertices.count)
            var v = VertexIn()
            v.position = pos
            v.normal = normal
            v.color = color
            v.texCoord = SIMD2<Float>(0, 0)
            vertices.append(v)
            return idx
        }

        for ed in edges {
            let d1 = SIMD3<Float>(ed.d1x, ed.d1y, ed.d1z)
            let d2 = SIMD3<Float>(ed.d2x, ed.d2y, ed.d2z)
            let center = SIMD3<Float>(ed.cx, ed.cy, ed.cz)

            let needsSplit = twoTone && (ed.axis == sa)

            for i in 0..<ROUND_N {
                let n0 = cs[i] * d1 + sn[i] * d2
                let n1 = cs[i+1] * d1 + sn[i+1] * d2
                let o0 = r * n0
                let o1 = r * n1

                if needsSplit {
                    // Two segments: lo..sc and sc..hi
                    let splits: [(Float, Float)] = [(ed.lo, sc), (sc, ed.hi)]
                    let segGreen = [isGreen(ed.lo), isGreen(ed.hi)]

                    for s in 0..<2 {
                        let sLo = splits[s].0
                        let sHi = splits[s].1

                        var p0 = center; p0[ed.axis] = sLo
                        var p1 = center; p1[ed.axis] = sHi

                        let a0 = p0 + o0, a1 = p1 + o0
                        let b0 = p0 + o1, b1 = p1 + o1

                        let c0 = vertexColor(n0, segGreen[s])
                        let c1 = vertexColor(n1, segGreen[s])

                        let vi0 = addV(a0, n0, c0)
                        let vi1 = addV(a1, n0, c0)
                        let vi2 = addV(b1, n1, c1)
                        let vi3 = addV(b0, n1, c1)

                        indices.append(contentsOf: [vi0, vi1, vi2, vi0, vi2, vi3])
                    }
                } else {
                    let edgeSplitCoord: Float = {
                        if sa == 1 { return ed.cy }
                        if sa == 2 { return ed.cz }
                        return 0
                    }()
                    let edgeGreen = twoTone && isGreen(edgeSplitCoord)

                    var p0 = center; p0[ed.axis] = ed.lo
                    var p1 = center; p1[ed.axis] = ed.hi

                    let a0 = p0 + o0, a1 = p1 + o0
                    let b0 = p0 + o1, b1 = p1 + o1

                    let c0 = vertexColor(n0, edgeGreen)
                    let c1 = vertexColor(n1, edgeGreen)

                    let vi0 = addV(a0, n0, c0)
                    let vi1 = addV(a1, n0, c0)
                    let vi2 = addV(b1, n1, c1)
                    let vi3 = addV(b0, n1, c1)

                    indices.append(contentsOf: [vi0, vi1, vi2, vi0, vi2, vi3])
                }
            }
        }
    }

    // MARK: - Face decal quad

    private static func buildFaceQuad(standing: Bool) -> ([VertexIn], [UInt32]) {
        var verts = [VertexIn]()
        let hw = TILE_W * 0.5 - ROUND_R
        let n: SIMD3<Float>

        if standing {
            let hh = TILE_H * 0.5 - ROUND_R
            let fz = TILE_D * 0.502
            n = SIMD3<Float>(0, 0, 1)
            // BL, BR, TR, TL
            let positions: [SIMD3<Float>] = [
                SIMD3<Float>(-hw, -hh, fz),
                SIMD3<Float>( hw, -hh, fz),
                SIMD3<Float>( hw,  hh, fz),
                SIMD3<Float>(-hw,  hh, fz),
            ]
            let uvs: [SIMD2<Float>] = [
                SIMD2<Float>(0, 1),
                SIMD2<Float>(1, 1),
                SIMD2<Float>(1, 0),
                SIMD2<Float>(0, 0),
            ]
            for i in 0..<4 {
                var v = VertexIn()
                v.position = positions[i]
                v.normal = n
                v.color = SIMD4<Float>(1, 1, 1, 1)
                v.texCoord = uvs[i]
                verts.append(v)
            }
        } else {
            let hd = TILE_H * 0.5 - ROUND_R
            let fy = TILE_D * 0.502
            n = SIMD3<Float>(0, 1, 0)
            let positions: [SIMD3<Float>] = [
                SIMD3<Float>(-hw, fy,  hd),
                SIMD3<Float>( hw, fy,  hd),
                SIMD3<Float>( hw, fy, -hd),
                SIMD3<Float>(-hw, fy, -hd),
            ]
            let uvs: [SIMD2<Float>] = [
                SIMD2<Float>(0, 1),
                SIMD2<Float>(1, 1),
                SIMD2<Float>(1, 0),
                SIMD2<Float>(0, 0),
            ]
            for i in 0..<4 {
                var v = VertexIn()
                v.position = positions[i]
                v.normal = n
                v.color = SIMD4<Float>(1, 1, 1, 1)
                v.texCoord = uvs[i]
                verts.append(v)
            }
        }

        let idxs: [UInt32] = [0, 1, 2, 0, 2, 3]
        return (verts, idxs)
    }

    // MARK: - Shadow quad

    private static func buildShadowQuad(width: Float, depth: Float) -> ([VertexIn], [UInt32]) {
        let hw = width * 0.55
        let hd = depth * 0.65
        let sy: Float = 0.003
        let n = SIMD3<Float>(0, 1, 0)
        let c = SIMD4<Float>(0, 0, 0, 0.176)

        var verts = [VertexIn]()
        let positions: [SIMD3<Float>] = [
            SIMD3<Float>(-hw, sy, -hd),
            SIMD3<Float>( hw, sy, -hd),
            SIMD3<Float>( hw, sy,  hd),
            SIMD3<Float>(-hw, sy,  hd),
        ]
        for pos in positions {
            var v = VertexIn()
            v.position = pos
            v.normal = n
            v.color = c
            v.texCoord = SIMD2<Float>(0, 0)
            verts.append(v)
        }

        let idxs: [UInt32] = [0, 1, 2, 0, 2, 3]
        return (verts, idxs)
    }
}

// SIMD3 subscript extension for axis-based access
extension SIMD3 where Scalar == Float {
    subscript(axis: Int) -> Float {
        get {
            switch axis {
            case 0: return x
            case 1: return y
            case 2: return z
            default: return 0
            }
        }
        set {
            switch axis {
            case 0: x = newValue
            case 1: y = newValue
            case 2: z = newValue
            default: break
            }
        }
    }
}
