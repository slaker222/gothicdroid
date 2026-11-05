package com.opengothic

import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** File operations of the "Explore" tab. Paths are limited to the given roots; long jobs run on one worker thread. */
class FsOps(private val roots: () -> List<Pair<String, File>>) : ModJobs {
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

    override fun note(msg: String) { this.msg = msg }
    override fun addDone(n: Long) { done += n }
    override val cancelled: Boolean get() = cancel

    override fun job(title: String, body: () -> Unit): Boolean {
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

    /** where `name` goes inside `dir`: an existing entry with the same name (any letter case: Data = data) is used, so it gets replaced / merged */
    private fun target(dir: File, name: String): File {
        val f = File(dir, name)
        if (f.exists()) return f
        return dir.listFiles()?.firstOrNull { it.name.equals(name, ignoreCase = true) } ?: f
    }

    /** a stream into `dst`: written to a temporary file next to it, then it replaces the old file (an interrupted copy keeps the old one) */
    private fun writeReplacing(dst: File, open: () -> java.io.InputStream) {
        if (dst.isDirectory) dst.deleteRecursively()
        val tmp = File(dst.parentFile, dst.name + ".ogpart")
        try {
            open().use { i -> tmp.outputStream().use { o ->
                val b = ByteArray(1 shl 18)
                while (true) { val n = i.read(b); if (n < 0) break; o.write(b, 0, n); done += n; if (cancel) throw RuntimeException("Cancelled") }
            } }
            if (!tmp.renameTo(dst)) {
                if (dst.exists() && !dst.delete()) throw RuntimeException("Cannot replace ${dst.name}")
                if (!tmp.renameTo(dst)) throw RuntimeException("Cannot write ${dst.name}")
            }
        } finally { if (tmp.exists()) tmp.delete() }
    }

    /** a folder is merged into an existing one, a file replaces an existing file (of the same name in any letter case) */
    private fun copyTree(src: File, dst: File) {
        if (cancel) throw RuntimeException("Cancelled")
        if (src.isDirectory) {
            if (dst.exists() && !dst.isDirectory) dst.delete()
            if (!dst.mkdirs() && !dst.isDirectory) throw RuntimeException("Cannot create ${dst.name}")
            src.listFiles()?.forEach { copyTree(it, target(dst, it.name)) }
        } else {
            msg = src.name
            writeReplacing(dst) { src.inputStream() }
        }
    }

    /** copies / moves `names` (full paths) into the folder `dstDir`; a name that exists is REPLACED (folders are merged);
     *  pasting into the folder the item is already in makes a copy named " (2)" */
    fun paste(srcs: List<String>, dstDir: String, move: Boolean): Boolean {
        val dst = canon(dstDir) ?: return false
        val files = srcs.mapNotNull { canon(it) }
        return job(if (move) "Moving…" else "Copying…") {
            for (s in files) {
                if (dst.path == s.path || dst.path.startsWith(s.path + "/")) { if (s.isDirectory) throw RuntimeException("Cannot put a folder into itself: ${s.name}") }
                var t = target(dst, s.name)
                if (t.exists() && t.canonicalPath == s.canonicalPath) {
                    if (move) continue                  // moved onto itself: nothing to do
                    t = unique(dst, s.name)             // the same folder: a copy, never a copy over itself
                }
                if (move && !t.exists() && s.renameTo(t)) continue
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

    /** a text file -> {text, enc}; UTF-8 when valid, otherwise Latin-1 (the bytes survive a round trip) */
    fun read(path: String): String {
        val f = canon(path) ?: return JSONObject().put("error", "Access denied").toString()
        if (!f.isFile) return JSONObject().put("error", "Not a file").toString()
        if (f.length() > 3L shl 20) return JSONObject().put("error", "File is too big for the editor (max 3 MB)").toString()
        val b = f.readBytes()
        val dec = Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT).onUnmappableCharacter(java.nio.charset.CodingErrorAction.REPORT)
        return try { JSONObject().put("text", dec.decode(java.nio.ByteBuffer.wrap(b)).toString()).put("enc", "utf8").toString() }
        catch (e: Exception) { JSONObject().put("text", String(b, Charsets.ISO_8859_1)).put("enc", "latin1").toString() }
    }

    fun write(path: String, text: String, enc: String): String {
        val f = canon(path) ?: return "Access denied"
        return try { f.writeBytes(text.toByteArray(if (enc == "latin1") Charsets.ISO_8859_1 else Charsets.UTF_8)); "" } catch (e: Exception) { e.message ?: "Cannot write" }
    }

    /** total size of a file / folder, in bytes */
    fun size(path: String): Long { val f = canon(path) ?: return 0; return if (f.isDirectory) f.walkTopDown().filter { it.isFile }.sumOf { it.length() } else f.length() }

    /** import: streams from the system picker into the folder; a file of the same name is replaced */
    fun importStreams(dstDir: String, items: List<Pair<String, () -> java.io.InputStream?>>): Boolean {
        val dst = canon(dstDir) ?: return false
        return job("Importing…") {
            for ((name, open) in items) {
                msg = name
                val safe = name.replace('/', '_')
                val t = target(dst, safe).let { if (it.isDirectory) unique(dst, safe) else it }   // never wipes a folder
                writeReplacing(t) { open() ?: throw RuntimeException("Cannot read $name") }
            }
        }
    }
}
