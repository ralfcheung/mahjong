import simd
import Foundation

class OrbitalCamera {
    var yaw: Float = 0.3          // slight angle to see 3D shape
    var pitch: Float = 25 * .pi / 180  // lower pitch to see front face
    var distance: Float = 3.5     // closer to tiles
    var target: SIMD3<Float> = SIMD3<Float>(0, 0.4, 0)  // center on tile body
    var fovY: Float = 50 * .pi / 180  // field of view in radians
    var nearPlane: Float = 0.1
    var farPlane: Float = 100

    var eye: SIMD3<Float> {
        let x = distance * cosf(pitch) * sinf(yaw)
        let y = distance * sinf(pitch)
        let z = distance * cosf(pitch) * cosf(yaw)
        return target + SIMD3<Float>(x, y, z)
    }

    func viewMatrix() -> float4x4 {
        float4x4.lookAt(eye: eye, center: target, up: SIMD3<Float>(0, 1, 0))
    }

    func projectionMatrix(aspect: Float) -> float4x4 {
        float4x4.perspective(fovYRadians: fovY, aspect: aspect, near: nearPlane, far: farPlane)
    }

    // MARK: - Interaction

    func orbit(deltaX: Float, deltaY: Float) {
        yaw += deltaX * 0.01
        pitch += deltaY * 0.01
        pitch = max(-(.pi / 2 - 0.01), min(.pi / 2 - 0.01, pitch))
    }

    func zoom(delta: Float) {
        distance *= 1.0 - delta * 0.1
        distance = max(0.5, min(50, distance))
    }

    func pan(deltaX: Float, deltaY: Float) {
        let right = SIMD3<Float>(cosf(yaw), 0, -sinf(yaw))
        let up = SIMD3<Float>(0, 1, 0)
        let scale: Float = distance * 0.002
        target += right * (-deltaX * scale) + up * (deltaY * scale)
    }

    func reset() {
        yaw = 0.3
        pitch = 25 * .pi / 180
        distance = 3.5
        target = SIMD3<Float>(0, 0.4, 0)
    }
}
