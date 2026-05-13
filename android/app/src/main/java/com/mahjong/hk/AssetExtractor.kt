package com.mahjong.hk

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileOutputStream

/**
 * Extracts assets from the APK to internal storage on first run.
 * The C++ code reads assets from the filesystem, so we need to
 * extract them from the compressed APK first.
 */
object AssetExtractor {

    private const val TAG = "AssetExtractor"
    private const val VERSION_KEY = "assets_version"
    private const val CURRENT_VERSION = 1

    fun extractIfNeeded(context: Context) {
        val prefs = context.getSharedPreferences("mahjong", Context.MODE_PRIVATE)
        val extractedVersion = prefs.getInt(VERSION_KEY, 0)

        if (extractedVersion >= CURRENT_VERSION) {
            Log.i(TAG, "Assets already extracted (version $extractedVersion)")
            return
        }

        Log.i(TAG, "Extracting assets to internal storage...")
        val startTime = System.currentTimeMillis()

        extractDirectory(context, "", context.filesDir)

        val elapsed = System.currentTimeMillis() - startTime
        Log.i(TAG, "Asset extraction complete in ${elapsed}ms")

        prefs.edit().putInt(VERSION_KEY, CURRENT_VERSION).apply()
    }

    private fun extractDirectory(context: Context, assetPath: String, targetDir: File) {
        val assetManager = context.assets
        val entries = assetManager.list(assetPath) ?: return

        if (entries.isEmpty()) {
            // This is a file, extract it
            extractFile(context, assetPath, File(targetDir, assetPath))
            return
        }

        // This is a directory
        for (entry in entries) {
            val fullPath = if (assetPath.isEmpty()) entry else "$assetPath/$entry"
            val subEntries = assetManager.list(fullPath)

            if (subEntries != null && subEntries.isNotEmpty()) {
                // Subdirectory
                extractDirectory(context, fullPath, targetDir)
            } else {
                // File
                extractFile(context, fullPath, File(targetDir, fullPath))
            }
        }
    }

    private fun extractFile(context: Context, assetPath: String, targetFile: File) {
        // Only extract asset directories we care about
        if (!assetPath.startsWith("fonts/") &&
            !assetPath.startsWith("tiles/") &&
            !assetPath.startsWith("model/")) {
            return
        }

        targetFile.parentFile?.mkdirs()

        try {
            context.assets.open(assetPath).use { input ->
                FileOutputStream(targetFile).use { output ->
                    input.copyTo(output)
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to extract $assetPath: ${e.message}")
        }
    }
}
