package com.mahjong.hk

import android.content.res.AssetManager

/**
 * JNI bridge to C++ game engine + Raylib renderer.
 */
object NativeLib {

    init {
        System.loadLibrary("mahjong-native")
    }

    /** Initialize Raylib, renderer, and game engine. */
    external fun init(width: Int, height: Int, assetManager: AssetManager, internalPath: String)

    /** Shut down and free all resources. */
    external fun shutdown()

    /** Called each frame to update game logic and render. */
    external fun updateAndRender(dt: Float)

    /** Single-finger tap at screen coordinates. */
    external fun handleTap(x: Float, y: Float)

    /** Two-finger gesture update (called each frame while 2 fingers are down). */
    external fun handleTwoFingerGesture(x1: Float, y1: Float, x2: Float, y2: Float)

    /** Called when two-finger gesture ends. */
    external fun endTwoFingerGesture()

    /** Double-tap detected (camera reset). */
    external fun handleDoubleTap()

    /** Get JSON snapshot of current game state. */
    external fun snapshotJSON(): String

    /** Get current game phase as integer. */
    external fun currentPhase(): Int

    /** Check if game is over. */
    external fun isGameOver(): Boolean
}
