package com.opengothic

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

// The sky textures of Gothic 1 and Gothic 2 (the "Sky" setting) are not in the app: they lie in the Cloudflare R2 bucket
// (https://files.gothicplay.fun/sky/<g1|g2>/<file>, sky/manifest.json = {game: {file: size}}) and are downloaded into
// <files>/sky/<g1|g2> when the player chooses a sky that needs them. The engine reads the plain files (OG_SKY_PACK_DIR).
object SkyPack {
    private const val TAG = "SkyPack"
    private const val BASE = "https://files.gothicplay.fun/sky/"
    private const val KEY_FILE = "SKYDAY_LAYER0_A0-C.TEX"   // the engine mounts a folder once this one is there: it goes last

    fun dir(ctx: Context): File = File(ctx.filesDir, "sky")

    // "idle", "loading <percent>", "ready", "error <text>" (per call of ensure)
    @Volatile var status = "idle"
    private var job: Thread? = null

    private fun open(url: String): HttpURLConnection {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 15000; c.readTimeout = 30000
        c.setRequestProperty("User-Agent", "OpenGothic-Android")
        return c
    }

    // sky setting: 1 = Gothic 1, 2 and 3 = Gothic 2, 0 = nothing to download
    fun gameOf(sky: Int): Int = when (sky) { 1 -> 1; 2, 3 -> 2; else -> 0 }

    fun isComplete(ctx: Context, game: Int): Boolean {
        val m = try { JSONObject(File(dir(ctx), "manifest.json").readText()).optJSONObject("g$game") } catch (e: Exception) { null } ?: return false
        return m.keys().asSequence().all { n -> File(dir(ctx), "g$game/$n").let { it.isFile && it.length() == m.optLong(n) } }
    }

    // starts the download (once at a time) of what is missing for this sky; the result in `status`
    @Synchronized
    fun ensure(ctx: Context, sky: Int) {
        val game = gameOf(sky)
        if (game == 0) { status = "idle"; return }
        if (isComplete(ctx, game)) { status = "ready"; return }
        if (job?.isAlive == true) return
        status = "loading 0"
        job = Thread {
            try { download(ctx, game); status = "ready" }
            catch (e: Exception) { Log.e(TAG, "download: ${e.message}"); status = "error " + (e.message ?: e.javaClass.simpleName) }
        }.also { it.start() }
    }

    private fun download(ctx: Context, game: Int) {
        val mc = open(BASE + "manifest.json")
        val text = try { if (mc.responseCode != 200) throw RuntimeException("HTTP ${mc.responseCode}"); mc.inputStream.bufferedReader().readText() } finally { mc.disconnect() }
        val all = JSONObject(text)
        val m = all.getJSONObject("g$game")
        val dst = File(dir(ctx), "g$game").also { it.mkdirs() }
        val names = m.keys().asSequence().filter { n -> File(dst, n).let { !(it.isFile && it.length() == m.getLong(n)) } }
            .sortedBy { it == KEY_FILE }.toList()
        val total = names.sumOf { m.getLong(it) }.coerceAtLeast(1)
        var done = 0L
        for (n in names) {
            val part = File(dst, "$n.part")
            val c = open("$BASE" + "g$game/$n")
            try {
                if (c.responseCode != 200) throw RuntimeException("$n: HTTP ${c.responseCode}")
                c.inputStream.use { i -> part.outputStream().use { o ->
                    val buf = ByteArray(65536)
                    while (true) {
                        val k = i.read(buf)
                        if (k < 0) break
                        o.write(buf, 0, k)
                        done += k
                        status = "loading ${(done * 100 / total).toInt().coerceAtMost(99)}"
                    }
                } }
            } finally { c.disconnect() }
            if (part.length() != m.getLong(n)) { part.delete(); throw RuntimeException("$n: wrong size") }
            File(dst, n).delete()
            if (!part.renameTo(File(dst, n))) throw RuntimeException("$n: cannot save")
        }
        File(dir(ctx), "manifest.json").also { f ->
            // the manifest of the games that are complete (several downloads add to the file)
            val cur = try { JSONObject(f.readText()) } catch (e: Exception) { JSONObject() }
            cur.put("g$game", m); f.writeText(cur.toString())
        }
    }
}
