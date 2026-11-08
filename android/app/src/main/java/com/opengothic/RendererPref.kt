package com.opengothic

import android.content.Context
import java.io.File

// The renderer of the game: Vulkan (the full one) or OpenGL ES (the WebGL 2 renderer of the browser version: textures and
// models, for phones where Vulkan does not work). A file of the app, not SharedPreferences: the game runs in its own
// process and reads it at start. Without Vulkan the engine takes OpenGL ES by itself, whatever is chosen here.
object RendererPref {
    private fun file(ctx: Context) = File(ctx.filesDir, "renderer")

    fun isGl(ctx: Context): Boolean = try { file(ctx).readText().trim() == "gl" } catch (e: Exception) { false }

    fun set(ctx: Context, gl: Boolean) {
        try { file(ctx).writeText(if (gl) "gl" else "vulkan") } catch (e: Exception) {}
    }

    // the values of the page's select (web/shell.html #renderer-sel): webgpu = the full renderer, webgl2 = the simple one
    fun pageValue(ctx: Context): String = if (isGl(ctx)) "webgl2" else "webgpu"
}
