package com.opengothic

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.util.Log
import androidx.core.content.FileProvider
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.Locale

/**
 * Updates from the GitHub releases of the project.
 *
 * Source: the releases list of REPO (tag `v0.39`, an `.apk` asset, the notes in the body).
 * Notes may carry other languages: a line `## lang:ru` starts the text for that language, a line `## lang:en`
 * (or nothing at all) is English. A new language = a new section in the release text, no code change.
 * The notes of every release newer than the installed one are shown together (a skipped version is not lost).
 */
class UpdateChecker(private val ctx: Context) {
    companion object {
        private const val TAG = "OpenGothic"
        const val REPO = "slaker222/Gothicdroid"
        private const val CHECK_EVERY_MS = 6L * 3600 * 1000
        private const val PREFS = "opengothic_update"

        /** "v0.38" / "0.38" -> [0, 38]; compared number by number ("0.9" < "0.10") */
        fun parse(v: String): List<Int> =
            v.trim().removePrefix("v").removePrefix("V").split('.', '-').mapNotNull { it.takeWhile(Char::isDigit).toIntOrNull() }

        fun newer(a: String, b: String): Boolean {
            val x = parse(a); val y = parse(b)
            for (i in 0 until maxOf(x.size, y.size)) {
                val d = x.getOrElse(i) { 0 } - y.getOrElse(i) { 0 }
                if (d != 0) return d > 0
            }
            return false
        }

        /** the text of the release for the language: the section `## lang:xx`, else the English one */
        fun notesFor(body: String, lang: String): String {
            val secs = HashMap<String, StringBuilder>()
            var cur = "en"
            for (line in body.replace("\r", "").split('\n')) {
                val m = Regex("^##\\s*lang:\\s*([A-Za-z-]+)\\s*$").find(line)
                if (m != null) { cur = m.groupValues[1].lowercase(); continue }
                secs.getOrPut(cur) { StringBuilder() }.append(line).append('\n')
            }
            return (secs[lang] ?: secs["en"] ?: secs.values.firstOrNull() ?: StringBuilder()).toString().trim()
        }
    }

    private val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private val dir get() = File(ctx.getExternalFilesDir(null), "updates").also { it.mkdirs() }

    @Volatile private var checking = false
    @Volatile private var dlRunning = false
    @Volatile private var dlDone = 0L
    @Volatile private var dlTotal = 0L
    @Volatile private var error = ""
    @Volatile private var ready: File? = null
    @Volatile var onChange: (() -> Unit)? = null

    fun current(): String = try { ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName ?: "0" } catch (e: Exception) { "0" }

    /** the check on the network (at most every 6 h unless `force`), then onChange */
    fun check(force: Boolean) {
        if (checking) return
        val stale = System.currentTimeMillis() - prefs.getLong("last", 0) > CHECK_EVERY_MS
        if (!force && !stale) { onChange?.invoke(); return }
        checking = true; error = ""
        Thread {
            try {
                val c = URL("https://api.github.com/repos/$REPO/releases?per_page=20").openConnection() as HttpURLConnection
                c.connectTimeout = 8000; c.readTimeout = 10000
                c.setRequestProperty("Accept", "application/vnd.github+json")
                c.setRequestProperty("User-Agent", "GothicDroid")
                prefs.getString("etag", null)?.let { if (prefs.contains("releases")) c.setRequestProperty("If-None-Match", it) }
                when (c.responseCode) {
                    304 -> prefs.edit().putLong("last", System.currentTimeMillis()).apply()
                    200 -> {
                        val text = c.inputStream.bufferedReader().readText()
                        val out = JSONArray()
                        val arr = JSONArray(text)
                        for (i in 0 until arr.length()) {
                            val r = arr.getJSONObject(i)
                            if (r.optBoolean("draft")) continue   // prereleases count: the project publishes them so
                            val assets = r.optJSONArray("assets") ?: continue
                            var apk: JSONObject? = null
                            for (j in 0 until assets.length()) {
                                val a = assets.getJSONObject(j)
                                if (a.optString("name").endsWith(".apk")) { apk = a; break }
                            }
                            if (apk == null) continue
                            out.put(JSONObject().put("tag", r.optString("tag_name")).put("body", r.optString("body"))
                                .put("url", apk.optString("browser_download_url")).put("size", apk.optLong("size"))
                                .put("sha", apk.optString("digest").removePrefix("sha256:")))
                        }
                        prefs.edit().putString("releases", out.toString()).putString("etag", c.getHeaderField("ETag"))
                            .putLong("last", System.currentTimeMillis()).apply()
                    }
                    else -> error = "GitHub answered ${c.responseCode}"
                }
                c.disconnect()
            } catch (e: Exception) {
                error = "No connection"
                Log.w(TAG, "update check: ${e.message}")
            }
            checking = false
            onChange?.invoke()
        }.start()
    }

    /** releases newer than the installed one, newest first */
    private fun newerReleases(): List<JSONObject> {
        val cur = current()
        val arr = try { JSONArray(prefs.getString("releases", "[]")) } catch (e: Exception) { JSONArray() }
        val l = ArrayList<JSONObject>()
        for (i in 0 until arr.length()) arr.getJSONObject(i).let { if (newer(it.optString("tag"), cur)) l.add(it) }
        l.sortWith { a, b -> if (newer(a.optString("tag"), b.optString("tag"))) -1 else if (newer(b.optString("tag"), a.optString("tag"))) 1 else 0 }
        return l
    }

    fun ignore() { newerReleases().firstOrNull()?.let { prefs.edit().putString("ignored", it.optString("tag")).apply(); onChange?.invoke() } }

    /** the state for the page */
    fun state(): String {
        val list = newerReleases()
        val o = JSONObject().put("current", current()).put("checking", checking).put("error", error)
            .put("last", prefs.getLong("last", 0))
            .put("downloading", dlRunning).put("done", dlDone).put("total", dlTotal)
            .put("ready", ready?.exists() == true)
        if (list.isNotEmpty()) {
            val lang = Locale.getDefault().language.lowercase()
            val top = list[0]
            val notes = JSONArray()
            for (r in list) notes.put(JSONObject().put("v", r.optString("tag").removePrefix("v")).put("text", notesFor(r.optString("body"), lang)))
            o.put("available", true).put("latest", top.optString("tag").removePrefix("v")).put("size", top.optLong("size"))
                .put("ignored", prefs.getString("ignored", "") == top.optString("tag")).put("notes", notes)
        } else o.put("available", false)
        return o.toString()
    }

    /** downloads the newest APK (resumes a partial file), checks it, then asks Android to install it */
    fun install() {
        ready?.takeIf { it.exists() }?.let { launchInstaller(it); return }
        if (dlRunning) return
        val r = newerReleases().firstOrNull() ?: return
        val url = r.optString("url"); val size = r.optLong("size"); val sha = r.optString("sha")
        val f = File(dir, "GothicDroid-${r.optString("tag").removePrefix("v")}.apk")
        dlRunning = true; error = ""; dlDone = 0; dlTotal = size
        onChange?.invoke()
        Thread {
            try {
                dir.listFiles()?.forEach { if (it.name != f.name) it.delete() }   // the older downloads
                val part = File(dir, f.name + ".part")
                if (!f.exists()) {
                    var have = if (part.exists()) part.length() else 0L
                    if (have > size) { part.delete(); have = 0 }
                    if (have < size) {
                        val c = URL(url).openConnection() as HttpURLConnection
                        c.connectTimeout = 10000; c.readTimeout = 20000
                        c.setRequestProperty("User-Agent", "GothicDroid")
                        if (have > 0) c.setRequestProperty("Range", "bytes=$have-")
                        val code = c.responseCode
                        if (code == 200) have = 0           // no resume: from the start
                        else if (code != 206) throw RuntimeException("Download failed ($code)")
                        dlDone = have
                        RandomAccessFile(part, "rw").use { out ->
                            out.seek(have); out.setLength(have)
                            c.inputStream.use { inp ->
                                val buf = ByteArray(64 * 1024)
                                while (true) {
                                    val n = inp.read(buf); if (n < 0) break
                                    out.write(buf, 0, n); dlDone += n
                                }
                            }
                        }
                        c.disconnect()
                    }
                    if (part.length() != size) { part.delete(); throw RuntimeException("The file is incomplete, try again") }
                    if (sha.isNotEmpty() && hash(part) != sha.lowercase()) { part.delete(); throw RuntimeException("Checksum mismatch") }
                    if (!part.renameTo(f)) throw RuntimeException("Cannot save the file")
                }
                val pi = ctx.packageManager.getPackageArchiveInfo(f.path, 0)
                if (pi == null || pi.packageName != ctx.packageName) { f.delete(); throw RuntimeException("Not a GothicDroid package") }
                ready = f
                dlRunning = false
                onChange?.invoke()
                launchInstaller(f)
                return@Thread
            } catch (e: Exception) {
                error = e.message ?: "Download failed"
                Log.w(TAG, "update download: ${e.message}")
            }
            dlRunning = false
            onChange?.invoke()
        }.start()
    }

    private fun hash(f: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        f.inputStream().use { i -> val b = ByteArray(64 * 1024); while (true) { val n = i.read(b); if (n < 0) break; md.update(b, 0, n) } }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    private fun launchInstaller(f: File) {
        if (!ctx.packageManager.canRequestPackageInstalls()) {
            // once: "Install unknown apps" for GothicDroid; the button works again after the return
            error = "Allow installing from GothicDroid, then press the button again"
            onChange?.invoke()
            ctx.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${ctx.packageName}")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            return
        }
        val uri = FileProvider.getUriForFile(ctx, "${ctx.packageName}.fileprovider", f)
        ctx.startActivity(Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK))
    }
}
