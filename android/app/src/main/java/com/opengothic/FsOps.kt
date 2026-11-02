package com.opengothic

import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** File operations of the "Explore" tab. Paths are limited to the given roots; long jobs run on one worker thread. */
class FsOps(private val roots: () -> List<Pair<String, File>>) {
    @Volatile var busy = false; private set
    @Volatile private var msg = ""
    @Volatile private var err = ""
    @Volatile private var done = 0L
    @Volatile private var cancel = false
    @Volatile private var finished = 0   // counts finished jobs: the page notices a change

    private fun canon(p: String): File? = try {
        val f = File(p).canonicalFile
        if (roots().any { f.path == it.second.canonicalPath || f.path.startsWith(it.second.canonicalPath + "/") }) f else null
    } catch (e: Exception) { null }

    fun rootsJson(): String = JSONArray().also { a -> roots().forEach { (n, f) -> a.put(JSONObject().put("name", n).put("path", f.path)) } }.toString()

    fun list(path: String): String {
        val d = canon(path) ?: return JSONObject().put("error", "Access denied").toString()
        val fs = d.listFiles() ?: return JSONObject().put("path", d.path).put("items", JSONArray()).put("error", "Cannot read this folder").toString()
        val items = JSONArray()
        fs.sortedWith(compareBy<File>({ !it.isDirectory }, { it.name.lowercase() })).forEach {
            items.put(JSONObject().put("n", it.name).put("d", it.isDirectory).put("s", if (it.isDirectory) (it.list()?.size ?: 0) else it.length()).put("t", it.lastModified()))
        }
        return JSONObject().put("path", d.path).put("items", items).toString()
    }

    fun state(): String = JSONObject().put("busy", busy).put("msg", msg).put("err", err).put("done", done).put("n", finished).toString()
    fun cancel() { cancel = true }

    private fun job(title: String, body: () -> Unit): Boolean {
        if (busy) return false
        busy = true; cancel = false; msg = title; err = ""; done = 0
        Thread {
            try { body() } catch (e: Exception) { err = e.message ?: e.toString() }
            if (cancel && err.isEmpty()) err = "Cancelled"
            busy = false; finished++
        }.start()
        return true
    }

    private fun unique(dir: File, name: String): File {
        var f = File(dir, name)
        if (!f.exists()) return f
        val dot = name.lastIndexOf('.').takeIf { it > 0 && !File(dir, name).isDirectory } ?: name.length
        var i = 2
        while (true) { f = File(dir, name.substring(0, dot) + " ($i)" + name.substring(dot)); if (!f.exists()) return f; i++ }
    }

    private fun copyTree(src: File, dst: File) {
        if (cancel) throw RuntimeException("Cancelled")
        if (src.isDirectory) {
            if (!dst.mkdirs() && !dst.isDirectory) throw RuntimeException("Cannot create ${dst.name}")
            src.listFiles()?.forEach { copyTree(it, File(dst, it.name)) }
        } else {
            msg = src.name
            src.inputStream().use { i -> dst.outputStream().use { o ->
                val b = ByteArray(1 shl 18)
                while (true) { val n = i.read(b); if (n < 0) break; o.write(b, 0, n); done += n; if (cancel) throw RuntimeException("Cancelled") }
            } }
        }
    }

    /** copies / moves `names` (full paths) into the folder `dstDir`; a name that exists gets " (2)" */
    fun paste(srcs: List<String>, dstDir: String, move: Boolean): Boolean {
        val dst = canon(dstDir) ?: return false
        val files = srcs.mapNotNull { canon(it) }
        return job(if (move) "Moving…" else "Copying…") {
            for (s in files) {
                if (dst.path == s.path || dst.path.startsWith(s.path + "/")) { if (s.isDirectory) throw RuntimeException("Cannot put a folder into itself: ${s.name}") }
                val t = unique(dst, s.name)
                if (move && s.renameTo(t)) continue
                copyTree(s, t)
                if (move) s.deleteRecursively()
            }
        }
    }

    fun delete(paths: List<String>): Boolean {
        val files = paths.mapNotNull { canon(it) }.filter { f -> roots().none { it.second.canonicalPath == f.path } }
        return job("Deleting…") { for (f in files) { msg = f.name; if (!f.deleteRecursively()) throw RuntimeException("Cannot delete ${f.name}") } }
    }

    fun mkdir(parent: String, name: String): String {
        val p = canon(parent) ?: return "Access denied"
        if (name.isBlank() || name.contains('/')) return "Bad name"
        val f = File(p, name.trim())
        return if (f.exists()) "Already exists" else if (f.mkdirs()) "" else "Cannot create the folder"
    }

    fun rename(path: String, name: String): String {
        val f = canon(path) ?: return "Access denied"
        if (name.isBlank() || name.contains('/')) return "Bad name"
        val t = File(f.parentFile, name.trim())
        return if (t.exists()) "Already exists" else if (f.renameTo(t)) "" else "Cannot rename"
    }

    /** total size of a file / folder, in bytes */
    fun size(path: String): Long { val f = canon(path) ?: return 0; return if (f.isDirectory) f.walkTopDown().filter { it.isFile }.sumOf { it.length() } else f.length() }

    /** import: streams from the system picker into the folder */
    fun importStreams(dstDir: String, items: List<Pair<String, () -> java.io.InputStream?>>): Boolean {
        val dst = canon(dstDir) ?: return false
        return job("Importing…") {
            for ((name, open) in items) {
                msg = name
                val t = unique(dst, name.replace('/', '_'))
                (open() ?: throw RuntimeException("Cannot read $name")).use { i -> t.outputStream().use { o ->
                    val b = ByteArray(1 shl 18)
                    while (true) { val n = i.read(b); if (n < 0) break; o.write(b, 0, n); done += n; if (cancel) { t.delete(); throw RuntimeException("Cancelled") } }
                } }
            }
        }
    }
}
