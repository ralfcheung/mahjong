// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "TilePreview",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(
            name: "TilePreview",
            path: ".",
            exclude: ["Package.swift"],
            resources: [
                .copy("Shaders.metal"),
            ],
            swiftSettings: [
                .unsafeFlags(["-import-objc-header", "ShaderTypes.h"])
            ]
        )
    ]
)
