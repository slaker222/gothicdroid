package com.opengothic

import android.content.Context
import android.os.Build
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.ZipInputStream

// Custom Vulkan drivers for Adreno GPUs (Mesa Turnip), in the "adrenotools" package format: a zip with meta.json and the
// driver .so. Installed into filesDir/drivers/<id>/, the choice is the file filesDir/vulkan_driver (the id; none = the system
// driver). Plain files, not SharedPreferences: the game runs in its own process (:game) and reads them at start.
object DriverStore {
    class Driver(val id: String, val name: String, val description: String, val author: String,
                 val driverVersion: String, val libraryName: String, val dir: File)

    class Result(val driver: Driver?, val error: String?)

    private const val MAX_TOTAL = 300L shl 20   // uncompressed size of a package
    private const val MAX_ENTRIES = 200

    private fun root(ctx: Context) = File(ctx.filesDir, "drivers")
    private fun sel(ctx: Context) = File(ctx.filesDir, "vulkan_driver")
    private fun trying(ctx: Context) = File(ctx.filesDir, "vulkan_driver_trying")
    private fun failed(ctx: Context) = File(ctx.filesDir, "vulkan_driver_failed")

    /** the device has an Adreno GPU (no GL context here: the launcher process has none) */
    fun supported(ctx: Context): Boolean {
        if (Build.VERSION.SDK_INT >= 31) {
            val m = Build.SOC_MANUFACTURER
            if (m.contains("Qualcomm", true) || m.contains("QTI", true)) return true
        }
        return Build.HARDWARE == "qcom" || File("/dev/kgsl-3d0").exists()
    }

    private fun read(dir: File): Driver? = try {
        val m = JSONObject(File(dir, "meta.json").readText())
        val lib = m.getString("libraryName")
        if (File(dir, lib).isFile) Driver(dir.name, m.optString("name", dir.name), m.optString("description"), m.optString("author"),
                                          m.optString("driverVersion"), lib, dir) else null
    } catch (e: Exception) { null }

    fun list(ctx: Context): List<Driver> =
        (root(ctx).listFiles() ?: emptyArray()).filter { it.isDirectory && !it.name.startsWith(".") }.mapNotNull { read(it) }.sortedBy { it.name.lowercase() }

    fun selectedId(ctx: Context): String = try { sel(ctx).readText().trim() } catch (e: Exception) { "" }

    fun select(ctx: Context, id: String?) {
        try { if (id.isNullOrEmpty()) sel(ctx).delete() else sel(ctx).writeText(id) } catch (e: Exception) {}
    }

    fun remove(ctx: Context, id: String) {
        if (id.isEmpty() || id.contains('/') || id.startsWith(".")) return
        if (selectedId(ctx) == id) select(ctx, null)
        File(root(ctx), id).deleteRecursively()
    }

    private fun slug(s: String) = s.lowercase().replace(Regex("[^a-z0-9._-]+"), "_").trim('_', '.').take(90)

    /** unpacks a package into a temp folder, checks it and moves it into place (an older copy with the same id is replaced) */
    fun importZip(ctx: Context, input: InputStream): Result {
        val rootDir = root(ctx).also { it.mkdirs() }
        val tmp = File(rootDir, ".tmp_${System.nanoTime()}")
        try {
            tmp.mkdirs()
            var total = 0L
            var n = 0
            ZipInputStream(input.buffered()).use { z ->
                val buf = ByteArray(64 * 1024)
                while (true) {
                    val e = z.nextEntry ?: break
                    if (e.isDirectory) continue
                    val name = e.name.replace('\\', '/')
                    if (name.startsWith("/") || name.split('/').any { it == ".." })
                        return Result(null, "Unsafe file name in the package")
                    if (++n > MAX_ENTRIES) return Result(null, "Too many files in the package")
                    val out = File(tmp, name)
                    if (!out.canonicalPath.startsWith(tmp.canonicalPath + File.separator)) return Result(null, "Unsafe file name in the package")
                    out.parentFile?.mkdirs()
                    out.outputStream().use { o ->
                        while (true) {
                            val r = z.read(buf)
                            if (r < 0) break
                            total += r
                            if (total > MAX_TOTAL) return Result(null, "The package is too big")
                            o.write(buf, 0, r)
                        }
                    }
                }
            }
            val meta = File(tmp, "meta.json")
            if (!meta.isFile) return Result(null, "Not a driver package: no meta.json")
            val m = try { JSONObject(meta.readText()) } catch (e: Exception) { return Result(null, "meta.json is broken") }
            val lib = m.optString("libraryName")
            if (lib.isEmpty() || lib.contains('/') || lib.contains('\\') || lib.startsWith(".")) return Result(null, "meta.json: bad libraryName")
            val so = File(tmp, lib)
            if (!so.isFile) return Result(null, "The driver file $lib is not in the package")
            val h = ByteArray(20)
            try { so.inputStream().use { it.read(h) } } catch (e: Exception) {}
            if (h[0] != 0x7f.toByte() || h[1] != 'E'.code.toByte() || h[2] != 'L'.code.toByte() || h[3] != 'F'.code.toByte())
                return Result(null, "$lib is not a native library")
            if (h[4].toInt() != 2 || (h[18].toInt() and 0xff) != 183 || h[19].toInt() != 0)
                return Result(null, "$lib is not for 64-bit ARM (aarch64)")
            if (m.optInt("minApi", 0) > Build.VERSION.SDK_INT) return Result(null, "The driver needs Android API ${m.optInt("minApi")}, this is ${Build.VERSION.SDK_INT}")
            val id = slug(listOf(m.optString("name"), m.optString("packageVersion"), m.optString("driverVersion")).filter { it.isNotEmpty() }.joinToString("_"))
            if (id.isEmpty()) return Result(null, "meta.json: no name")
            val dst = File(rootDir, id)
            val old = File(rootDir, ".old_${System.nanoTime()}")
            if (dst.exists() && !dst.renameTo(old)) return Result(null, "Cannot replace the installed driver")
            if (!tmp.renameTo(dst)) { old.renameTo(dst); return Result(null, "Cannot install the driver") }
            old.deleteRecursively()
            return read(dst)?.let { Result(it, null) } ?: Result(null, "Cannot install the driver")
        } catch (e: Exception) {
            return Result(null, "Cannot read the package: ${e.message ?: e.javaClass.simpleName}")
        } finally {
            tmp.deleteRecursively()
        }
    }

    // --- the game process ---
    @Volatile private var started = false
    private var startDriver: Driver? = null

    /**
     * The driver for this start of the game, or null (system driver). A marker file is there from now until the game really
     * renders (markWorking): if it is found at the next start, the driver crashed or hung - it is deselected and takeFailure
     * tells the launcher.
     */
    @Synchronized
    fun startInfo(ctx: Context): Driver? {
        if (started) return startDriver   // the same process again (activity recreated): the marker is not ours to judge
        started = true
        val id = selectedId(ctx)
        if (id.isEmpty() || !supported(ctx)) return null
        val d = list(ctx).firstOrNull { it.id == id }
        if (d == null) { select(ctx, null); return null }
        val mark = trying(ctx)
        if (mark.exists()) {
            select(ctx, null)
            try { failed(ctx).writeText(d.name) } catch (e: Exception) {}
            mark.delete()
            return null
        }
        try { mark.writeText(id) } catch (e: Exception) {}
        startDriver = d
        return d
    }

    fun markWorking(ctx: Context) { trying(ctx).delete() }

    /** the name of the driver that did not start (once), else null */
    fun takeFailure(ctx: Context): String? = try {
        val f = failed(ctx)
        if (f.isFile) f.readText().trim().also { f.delete() }.ifEmpty { null } else null
    } catch (e: Exception) { null }

    // --- the page ---
    fun json(d: Driver) = JSONObject().put("id", d.id).put("name", d.name).put("description", d.description)
        .put("author", d.author).put("version", d.driverVersion)

    fun stateJson(ctx: Context): String {
        val a = JSONArray()
        for (d in list(ctx)) a.put(json(d))
        return JSONObject().put("supported", supported(ctx)).put("selected", selectedId(ctx))
            .put("failed", takeFailure(ctx) ?: "").put("list", a).toString()
    }
}

// The packages to download: the .zip assets of the GitHub releases of REPO (Turnip builds for adrenotools and some
// repacks of Qualcomm drivers), newest first.
object DriverRepo {
    const val REPO = "K11MCH1/AdrenoToolsDrivers"
    private val HOSTS = listOf("github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com")

    class Asset(val release: String, val date: String, val name: String, val size: Long, val url: String)

    @Volatile private var cache: List<Asset>? = null

    private fun hostOk(u: URL) = u.protocol == "https" && HOSTS.any { u.host == it }

    /** blocking: call from a thread. Throws an Exception with a message for the user */
    fun fetchReleases(): List<Asset> {
        cache?.let { return it }
        try {
            val c = URL("https://api.github.com/repos/$REPO/releases?per_page=30").openConnection() as HttpURLConnection
            c.connectTimeout = 8000; c.readTimeout = 15000
            c.setRequestProperty("Accept", "application/vnd.github+json")
            c.setRequestProperty("User-Agent", "GothicDroid")
            val code = c.responseCode
            if (code == 403 || code == 429) throw Exception("GitHub limits the requests, try again later")
            if (code != 200) throw Exception("GitHub answered $code")
            val arr = JSONArray(c.inputStream.bufferedReader().readText())
            c.disconnect()
            val out = ArrayList<Asset>()
            for (i in 0 until arr.length()) {
                val r = arr.getJSONObject(i)
                if (r.optBoolean("draft")) continue
                val rel = r.optString("name").trim().ifEmpty { r.optString("tag_name") }
                val assets = r.optJSONArray("assets") ?: continue
                for (j in 0 until assets.length()) {
                    val a = assets.getJSONObject(j)
                    val n = a.optString("name")
                    val u = a.optString("browser_download_url")
                    if (n.endsWith(".zip", true) && u.isNotEmpty()) out.add(Asset(rel, r.optString("published_at").take(10), n, a.optLong("size"), u))
                }
            }
            cache = out
            return out
        } catch (e: java.io.IOException) {
            throw Exception("No connection")
        }
    }

    fun releasesJson(list: List<Asset>): String {
        val a = JSONArray()
        for (x in list) a.put(JSONObject().put("release", x.release).put("date", x.date).put("name", x.name).put("size", x.size).put("url", x.url))
        return a.toString()
    }

    /** downloads a package into the cache and installs it; blocking */
    fun download(ctx: Context, url: String, progress: (Long, Long) -> Unit): DriverStore.Result {
        val tmp = File(ctx.cacheDir, "driver_download.zip")
        try {
            var u = URL(url)
            var c: HttpURLConnection? = null
            for (hop in 0..5) {
                if (!hostOk(u)) return DriverStore.Result(null, "This download address is not allowed")
                c = u.openConnection() as HttpURLConnection
                c.instanceFollowRedirects = false
                c.connectTimeout = 8000; c.readTimeout = 20000
                c.setRequestProperty("User-Agent", "GothicDroid")
                val code = c.responseCode
                if (code in 300..399) {
                    val loc = c.getHeaderField("Location") ?: return DriverStore.Result(null, "Bad answer of the server")
                    u = URL(u, loc); c.disconnect(); c = null
                    continue
                }
                if (code != 200) return DriverStore.Result(null, "The server answered $code")
                break
            }
            val conn = c ?: return DriverStore.Result(null, "Too many redirects")
            val total = conn.contentLengthLong
            var done = 0L
            conn.inputStream.use { i ->
                tmp.outputStream().use { o ->
                    val buf = ByteArray(64 * 1024)
                    var last = 0L
                    while (true) {
                        val r = i.read(buf)
                        if (r < 0) break
                        o.write(buf, 0, r); done += r
                        val now = System.currentTimeMillis()
                        if (now - last > 150) { last = now; progress(done, total) }
                    }
                }
            }
            conn.disconnect()
            progress(done, total)
            return tmp.inputStream().use { DriverStore.importZip(ctx, it) }
        } catch (e: java.io.IOException) {
            return DriverStore.Result(null, "Download failed: ${e.message ?: "no connection"}")
        } finally {
            tmp.delete()
        }
    }
}
