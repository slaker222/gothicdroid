package com.opengothic

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.zip.CRC32
import java.util.zip.Inflater

/**
 * Installs a game from the server of the PC (web/serve.py, the same packs the browser version installs):
 * <server>/lang/languages.json (Gothic 1) or <server>/lang_g2/languages.json (Gothic 2) lists a base pack and,
 * per language, a text pack and a voice pack. Every pack is manifest.json + chunks made by tools/pack_web.py:
 * a chunk holds pieces of files (stored or deflate-raw), the manifest says where each piece goes.
 * The server is a plain http address in the local network (serve.py without --https); https works when its
 * certificate is valid.
 */
class PackDownloader(private val server: String) {

    class Cancelled : Exception()

    class Lang(val id: String, val name: String, val textSize: Long, val voiceSize: Long, val hasVoice: Boolean)
    class Index(val baseSize: Long, val langs: List<Lang>, val dir: String)

    // progress for the page
    @Volatile var phase = ""
    @Volatile var error = ""
    @Volatile var running = false
    @Volatile var finished = false
    val done = AtomicLong()
    @Volatile var total = 0L
    @Volatile private var cancel = false

    fun cancel() { cancel = true }

    companion object {
        fun langDir(game: Int) = if (game == 2) "lang_g2" else "lang"
    }

    private fun base(): String = if (server.endsWith("/")) server else "$server/"

    private fun open(url: String): HttpURLConnection {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 8000
        c.readTimeout = 20000
        c.setRequestProperty("Cache-Control", "no-cache")
        return c
    }

    private fun getBytes(url: String): ByteArray {
        val c = open(url)
        if (c.responseCode != 200) throw Exception("HTTP ${c.responseCode}: $url")
        return c.inputStream.use { it.readBytes() }
    }

    private fun getJson(url: String) = JSONObject(String(getBytes(url), Charsets.UTF_8))

    fun index(game: Int): Index {
        val dir = langDir(game)
        val j = getJson(base() + "$dir/languages.json")
        val langs = ArrayList<Lang>()
        val a = j.getJSONArray("languages")
        for (i in 0 until a.length()) {
            val l = a.getJSONObject(i)
            val t = l.optJSONObject("text")
            val v = l.optJSONObject("voice")
            if (t == null) continue
            langs.add(Lang(l.getString("id"), l.optString("name", l.getString("id")), t.getLong("size"), v?.getLong("size") ?: 0L, v != null))
        }
        return Index(j.getJSONObject("base").getLong("size"), langs, dir)
    }

    /**
     * installs base + the text pack + the voice pack of the chosen languages into [target].
     * "" keeps the language that is installed (one language is changed alone), voice "none" is no voice pack at all.
     */
    fun install(game: Int, textSel: String, voiceSel: String, target: File) {
        running = true; finished = false; error = ""; cancel = false; done.set(0)
        try {
            val idx = index(game)
            val cur = try { JSONObject(File(target, ".og_langs.json").readText()) } catch (e: Exception) { null }
            val text = if (textSel.isEmpty()) cur?.optString("text", "") ?: "" else textSel
            val voice = if (voiceSel.isEmpty()) cur?.optString("voice", "none") ?: "none" else voiceSel
            val t = idx.langs.firstOrNull { it.id == text } ?: throw Exception("no text pack: $text")
            val v = if (voice == "none") null else idx.langs.firstOrNull { it.id == voice && it.hasVoice } ?: throw Exception("no voice pack: $voice")
            target.mkdirs()
            val packs = ArrayList<Pair<String, String>>()
            packs.add(Pair("base", "${idx.dir}/base/manifest.json"))
            packs.add(Pair("text-$text", "${idx.dir}/$text/text/manifest.json"))
            if (v != null) packs.add(Pair("voice-$voice", "${idx.dir}/$voice/voice/manifest.json"))
            // what is still to be downloaded (packs that are installed already are not counted)
            phase = "check"
            val manifests = packs.map { (key, path) -> getJson(base() + path).also { it.put("_key", key) } }
            total = manifests.sumOf { remaining(target, it) }
            val free = target.usableSpace
            if (free < total * 2) throw Exception("Not enough space: need about ${total * 2 / 1048576} MB (the unpacked game is larger than the download), free ${free / 1048576} MB")
            val files = JSONObject()
            for ((i, p) in packs.withIndex()) {
                phase = p.first
                files.put(p.first.substringBefore('-'), installPack(target, p.first, base() + p.second, manifests[i]))
            }
            // the files of the languages that were installed before and are not in this choice
            removeOld(target, files)
            // the state of a pack whose files are gone: it must not say "done" if the language comes back
            if (cur != null) {
                if (cur.optString("text") != text) File(target, ".og_pack_text-${cur.optString("text")}.json").delete()
                if (cur.optString("voice") != voice && cur.optString("voice") != "none") File(target, ".og_pack_voice-${cur.optString("voice")}.json").delete()
            }
            File(target, ".og_langs.json").writeText(JSONObject().put("text", text).put("voice", if (v != null) voice else "none").put("files", files).toString())
            phase = "done"
            finished = true
        } catch (e: Cancelled) {
            error = "cancelled"
        } catch (e: Exception) {
            error = e.message ?: e.toString()
        } finally {
            running = false
        }
    }

    private fun removeOld(target: File, now: JSONObject) {
        val old = try { JSONObject(File(target, ".og_langs.json").readText()).getJSONObject("files") } catch (e: Exception) { return }
        val keep = HashSet<String>()
        for (k in now.keys()) { val a = now.getJSONArray(k); for (i in 0 until a.length()) keep.add(a.getString(i).lowercase()) }
        for (k in old.keys()) {
            if (k == "base") continue
            val a = old.getJSONArray(k)
            for (i in 0 until a.length()) {
                val p = a.getString(i)
                if (p.lowercase() !in keep) File(target, p).delete()
            }
        }
    }

    // bytes of the chunks of a pack that are not installed yet
    private fun remaining(target: File, man: JSONObject): Long {
        val done = HashSet<String>()
        try { val a = JSONObject(File(target, ".og_pack_${man.getString("_key")}.json").readText()).getJSONArray("done"); for (i in 0 until a.length()) done.add(a.getString(i)) } catch (e: Exception) {}
        var n = 0L
        val chunks = man.getJSONArray("chunks")
        for (i in 0 until chunks.length()) chunks.getJSONObject(i).let { if (it.getString("name") !in done) n += it.getLong("size") }
        return n
    }

    /** returns the paths of the files of the pack */
    private fun installPack(target: File, key: String, manifestUrl: String, man: JSONObject): JSONArray {
        if (man.getInt("version") != 1) throw Exception("unsupported manifest version")
        val folder = manifestUrl.substringBeforeLast('/') + "/"
        val files = man.getJSONArray("files")
        val chunks = man.getJSONArray("chunks")
        val pieces = man.getJSONArray("pieces")
        val state = File(target, ".og_pack_$key.json")
        val doneNames = HashSet<String>()
        try { val a = JSONObject(state.readText()).getJSONArray("done"); for (i in 0 until a.length()) doneNames.add(a.getString(i)) } catch (e: Exception) {}

        // the pieces of every chunk
        val byChunk = Array(chunks.length()) { ArrayList<JSONObject>() }
        for (i in 0 until pieces.length()) pieces.getJSONObject(i).let { byChunk[it.getInt("chunk")].add(it) }

        // every file exists with its final size (a piece is written at its place)
        val made = HashSet<Int>()
        fun prepare(fi: Int): File {
            val f = files.getJSONObject(fi)
            val file = File(target, f.getString("path"))
            synchronized(made) {
                if (made.add(fi)) {
                    file.parentFile?.mkdirs()
                    RandomAccessFile(file, "rw").use { if (it.length() != f.getLong("size")) it.setLength(f.getLong("size")) }
                }
            }
            return file
        }

        val todo = (0 until chunks.length()).filter { chunks.getJSONObject(it).getString("name") !in doneNames }
        val next = AtomicInteger()
        var failure: Exception? = null
        val lanes = (1..minOf(3, maxOf(1, todo.size))).map {
            Thread {
                try {
                    while (failure == null) {
                        val n = next.getAndIncrement()
                        if (n >= todo.size) break
                        val ci = todo[n]
                        val c = chunks.getJSONObject(ci)
                        val data = fetchChunk(folder + c.getString("name"), c.getLong("size"), c.getString("sha256"))
                        for (p in byChunk[ci]) {
                            val file = prepare(p.getInt("file"))
                            var raw = data.copyOfRange(p.getInt("off"), p.getInt("off") + p.getInt("len"))
                            if (p.optString("method") == "deflate-raw") raw = inflate(raw, p.getInt("raw"))
                            val crc = CRC32().apply { update(raw) }.value
                            if (raw.size != p.getInt("raw") || crc != p.getLong("crc")) throw Exception("corrupt pack: ${file.name}")
                            RandomAccessFile(file, "rw").use { it.seek(p.getLong("at")); it.write(raw) }
                        }
                        synchronized(doneNames) {
                            doneNames.add(c.getString("name"))
                            state.writeText(JSONObject().put("done", JSONArray(doneNames.toList())).toString())
                        }
                    }
                } catch (e: Exception) {
                    if (failure == null) failure = e
                }
            }.also { it.start() }
        }
        lanes.forEach { it.join() }
        failure?.let { throw it }
        // files no chunk wrote into (empty ones)
        for (fi in 0 until files.length()) prepare(fi)

        val paths = JSONArray()
        for (fi in 0 until files.length()) paths.put(files.getJSONObject(fi).getString("path"))
        return paths
    }

    private fun inflate(src: ByteArray, size: Int): ByteArray {
        val inf = Inflater(true)
        inf.setInput(src)
        val out = ByteArray(size)
        var at = 0
        while (at < size && !inf.finished()) {
            val n = inf.inflate(out, at, size - at)
            if (n == 0 && (inf.needsInput() || inf.needsDictionary())) break
            at += n
        }
        inf.end()
        if (at != size) throw Exception("corrupt deflate data")
        return out
    }

    private fun fetchChunk(url: String, size: Long, sha: String): ByteArray {
        var lastErr: Exception? = null
        for (attempt in 1..5) {
            if (cancel) throw Cancelled()
            var got = 0L
            try {
                val c = open(url)
                if (c.responseCode != 200) throw Exception("HTTP ${c.responseCode}: ${url.substringAfterLast('/')}")
                val buf = ByteArray(size.toInt())
                c.inputStream.use { s: InputStream ->
                    var at = 0
                    while (at < buf.size) {
                        if (cancel) throw Cancelled()
                        val n = s.read(buf, at, minOf(65536, buf.size - at))
                        if (n < 0) throw Exception("connection lost")
                        at += n
                        done.addAndGet(n.toLong()); got += n
                    }
                    if (s.read() >= 0) throw Exception("unexpected size of ${url.substringAfterLast('/')}")
                }
                val h = MessageDigest.getInstance("SHA-256").digest(buf).joinToString("") { "%02x".format(it) }
                if (h != sha) throw Exception("corrupt download: ${url.substringAfterLast('/')}")
                return buf
            } catch (e: Cancelled) {
                throw e
            } catch (e: Exception) {
                done.addAndGet(-got)
                lastErr = e
                try { Thread.sleep(1000L * attempt) } catch (_: InterruptedException) {}
            }
        }
        throw lastErr ?: Exception("download failed")
    }
}
