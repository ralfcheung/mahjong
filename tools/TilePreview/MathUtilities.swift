import simd

extension float4x4 {
    // MARK: - Factory methods

    static func perspective(fovYRadians fovy: Float, aspect: Float, near: Float, far: Float) -> float4x4 {
        let y = 1.0 / tanf(fovy * 0.5)
        let x = y / aspect
        let z = far / (near - far)
        return float4x4(columns: (
            SIMD4<Float>(x, 0, 0,  0),
            SIMD4<Float>(0, y, 0,  0),
            SIMD4<Float>(0, 0, z, -1),
            SIMD4<Float>(0, 0, z * near, 0)
        ))
    }

    static func lookAt(eye: SIMD3<Float>, center: SIMD3<Float>, up: SIMD3<Float>) -> float4x4 {
        let f = normalize(center - eye)
        let s = normalize(cross(f, up))
        let u = cross(s, f)
        return float4x4(columns: (
            SIMD4<Float>(s.x, u.x, -f.x, 0),
            SIMD4<Float>(s.y, u.y, -f.y, 0),
            SIMD4<Float>(s.z, u.z, -f.z, 0),
            SIMD4<Float>(-dot(s, eye), -dot(u, eye), dot(f, eye), 1)
        ))
    }

    static func translation(_ t: SIMD3<Float>) -> float4x4 {
        var m = matrix_identity_float4x4
        m.columns.3 = SIMD4<Float>(t.x, t.y, t.z, 1)
        return m
    }

    static func scale(_ s: SIMD3<Float>) -> float4x4 {
        var m = matrix_identity_float4x4
        m.columns.0.x = s.x
        m.columns.1.y = s.y
        m.columns.2.z = s.z
        return m
    }

    static func rotationX(_ radians: Float) -> float4x4 {
        let c = cosf(radians)
        let s = sinf(radians)
        return float4x4(columns: (
            SIMD4<Float>(1, 0,  0, 0),
            SIMD4<Float>(0, c,  s, 0),
            SIMD4<Float>(0, -s, c, 0),
            SIMD4<Float>(0, 0,  0, 1)
        ))
    }

    static func rotationY(_ radians: Float) -> float4x4 {
        let c = cosf(radians)
        let s = sinf(radians)
        return float4x4(columns: (
            SIMD4<Float>(c, 0, -s, 0),
            SIMD4<Float>(0, 1,  0, 0),
            SIMD4<Float>(s, 0,  c, 0),
            SIMD4<Float>(0, 0,  0, 1)
        ))
    }

    static func rotationZ(_ radians: Float) -> float4x4 {
        let c = cosf(radians)
        let s = sinf(radians)
        return float4x4(columns: (
            SIMD4<Float>( c, s, 0, 0),
            SIMD4<Float>(-s, c, 0, 0),
            SIMD4<Float>( 0, 0, 1, 0),
            SIMD4<Float>( 0, 0, 0, 1)
        ))
    }

    // MARK: - Utilities

    var upperLeft3x3: float3x3 {
        float3x3(
            SIMD3<Float>(columns.0.x, columns.0.y, columns.0.z),
            SIMD3<Float>(columns.1.x, columns.1.y, columns.1.z),
            SIMD3<Float>(columns.2.x, columns.2.y, columns.2.z)
        )
    }
}
