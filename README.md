# Mahjong

## Build and Run

Run commands from the repo root:

```bash
cd /Users/ralfcheung/code/mahjong
```

### Desktop

Configure and build the CMake/raylib desktop app:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(sysctl -n hw.ncpu)"
./build/mahjong
```

Optional tests:

```bash
./build/test_scoring
./build/test_atlas_tiles
```

The first CMake configure may need internet access because the project fetches
`raylib` through CMake.

### iOS

Build the C++ core static libraries that the Xcode app links against:

```bash
cd ios
./build_ios.sh
cd ..
```

Then open the Xcode project:

```bash
open ios/MahjongHK/Mahjong/Mahjong.xcodeproj
```

In Xcode, select the `Mahjong` scheme, choose an iOS Simulator or device, then
run the app.

For a command-line build:

```bash
xcodebuild \
  -project ios/MahjongHK/Mahjong/Mahjong.xcodeproj \
  -scheme Mahjong \
  -configuration Debug \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath build/XcodeDerivedData \
  build
```

For a physical iPhone, select your Apple Developer team in Xcode if code signing
requires it. The simulator build is configured for Apple Silicon simulator
(`arm64`) only.
