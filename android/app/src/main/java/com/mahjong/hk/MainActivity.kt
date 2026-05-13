package com.mahjong.hk

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.hypot

/**
 * Main activity for Mahjong HK.
 * Uses a GLSurfaceView with Raylib rendering via JNI.
 */
class MainActivity : Activity() {

    private lateinit var glView: GameGLSurfaceView
    private var lastFrameTime = System.nanoTime()
    private var initialized = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Fullscreen immersive
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        )

        // Extract assets to internal storage on first run
        AssetExtractor.extractIfNeeded(this)

        glView = GameGLSurfaceView(this)
        setContentView(glView)
    }

    override fun onPause() {
        super.onPause()
        glView.onPause()
    }

    override fun onResume() {
        super.onResume()
        glView.onResume()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (initialized) {
            NativeLib.shutdown()
            initialized = false
        }
    }

    /**
     * Custom GLSurfaceView that handles touch input and drives the game loop.
     */
    inner class GameGLSurfaceView(activity: MainActivity) : GLSurfaceView(activity) {

        // Touch tracking
        private val activeTouches = mutableMapOf<Int, Pair<Float, Float>>()
        private var lastTapTime = 0L
        private var lastTapX = 0f
        private var lastTapY = 0f

        init {
            setEGLContextClientVersion(2)
            setRenderer(GameRenderer())
            renderMode = RENDERMODE_CONTINUOUSLY
        }

        override fun onTouchEvent(event: MotionEvent): Boolean {
            val pointerIndex = event.actionIndex
            val pointerId = event.getPointerId(pointerIndex)

            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    activeTouches[pointerId] = Pair(event.getX(pointerIndex), event.getY(pointerIndex))

                    if (activeTouches.size == 2) {
                        val pts = activeTouches.values.toList()
                        queueEvent {
                            NativeLib.handleTwoFingerGesture(
                                pts[0].first, pts[0].second,
                                pts[1].first, pts[1].second
                            )
                        }
                    }
                }

                MotionEvent.ACTION_MOVE -> {
                    // Update all touch positions
                    for (i in 0 until event.pointerCount) {
                        val id = event.getPointerId(i)
                        activeTouches[id] = Pair(event.getX(i), event.getY(i))
                    }

                    if (activeTouches.size == 2) {
                        val pts = activeTouches.values.toList()
                        queueEvent {
                            NativeLib.handleTwoFingerGesture(
                                pts[0].first, pts[0].second,
                                pts[1].first, pts[1].second
                            )
                        }
                    }
                }

                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                    val wasMulti = activeTouches.size >= 2
                    activeTouches.remove(pointerId)

                    if (wasMulti) {
                        queueEvent { NativeLib.endTwoFingerGesture() }
                        return true
                    }

                    // Single finger release → tap
                    if (activeTouches.isEmpty) {
                        val x = event.getX(pointerIndex)
                        val y = event.getY(pointerIndex)
                        val now = System.currentTimeMillis()

                        // Double-tap detection
                        val dist = hypot((x - lastTapX).toDouble(), (y - lastTapY).toDouble())
                        if (now - lastTapTime < 350 && dist < 30.0) {
                            queueEvent { NativeLib.handleDoubleTap() }
                            lastTapTime = 0
                            return true
                        }

                        lastTapTime = now
                        lastTapX = x
                        lastTapY = y

                        queueEvent { NativeLib.handleTap(x, y) }
                    }
                }

                MotionEvent.ACTION_CANCEL -> {
                    activeTouches.clear()
                    queueEvent { NativeLib.endTwoFingerGesture() }
                }
            }

            return true
        }

        /**
         * GLSurfaceView.Renderer that drives the Mahjong game loop.
         */
        inner class GameRenderer : Renderer {

            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                val display = this@MainActivity.windowManager.defaultDisplay
                val size = android.graphics.Point()
                display.getRealSize(size)

                NativeLib.init(
                    size.x, size.y,
                    this@MainActivity.assets,
                    this@MainActivity.filesDir.absolutePath
                )

                this@MainActivity.initialized = true
                lastFrameTime = System.nanoTime()
            }

            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                // Raylib handles viewport resize internally
            }

            override fun onDrawFrame(gl: GL10?) {
                val now = System.nanoTime()
                val dt = (now - lastFrameTime) / 1_000_000_000f
                lastFrameTime = now

                NativeLib.updateAndRender(dt)
            }
        }
    }
}
