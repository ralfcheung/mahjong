#pragma once

#import <Foundation/Foundation.h>

// ObjC++ bridge between Swift UI layer and C++ game engine + Raylib renderer.
// Called from GameViewController via CADisplayLink.

@interface RaylibBridge : NSObject

/// Initialize Raylib window, renderer, and game engine.
/// Must be called once after the GL view is ready.
/// @param width Screen width in pixels
/// @param height Screen height in pixels
/// @param scaleFactor Device scale factor (retina)
/// @param bundlePath Path to the app bundle's resource directory
- (void)initWithWidth:(int)width
               height:(int)height
          scaleFactor:(float)scaleFactor
           bundlePath:(NSString *)bundlePath;

/// Shut down Raylib and free all resources.
- (void)shutdown;

/// Called each frame by CADisplayLink.
/// @param dt Delta time since last frame
- (void)updateAndRender:(float)dt;

/// Process a single-finger tap at screen coordinates.
/// Used for tile selection and UI button taps.
/// @param x Screen X coordinate
/// @param y Screen Y coordinate
- (void)handleTap:(float)x y:(float)y;

/// Process two-finger gesture state each frame.
/// @param x1 First touch X
/// @param y1 First touch Y
/// @param x2 Second touch X
/// @param y2 Second touch Y
- (void)handleTwoFingerGesture:(float)x1
                             y1:(float)y1
                             x2:(float)x2
                             y2:(float)y2;

/// Called when two-finger gesture ends (finger lifted).
- (void)endTwoFingerGesture;

/// Process a double-tap (camera reset).
- (void)handleDoubleTap;

/// Get current game phase as integer.
- (int)currentPhase;

/// Check if game is over.
- (BOOL)isGameOver;

/// Get JSON snapshot of current game state.
- (NSString *)snapshotJSON;

@end
