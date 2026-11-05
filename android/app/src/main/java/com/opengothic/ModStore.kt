package com.opengothic

import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.zip.Deflater
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/** A long job with progress that the launcher shows (FsOps implements it) */
interface ModJobs {
    fun job(title: String, body: () -> Unit): Boolean
    fun note(msg: String)
    fun addDone(n: Long)
    val cancelled: Boolean
}

/**
 * Mods imported from a .zip. A mod is unpacked into the folder of a game (Gothic 1 / Gothic 2) and bound to it.
 * Switching it off packs its files back into <root>/<id>.zip and removes them from the game (a file of the game that
 * the mod had replaced is put back); switching it on unpacks the zip again.
 *
 * The zip is a folder tree like the game's own: Data/ModVDF/x.mod, system/x.ini, _work/... (an extra top folder is fine).
 * Pure JVM on purpose: no Android classes, so that it can be tested on a PC.
 */
class ModStore(private val jobs: ModJobs, private val root: File, private val gameDir: (Int) -> File?) {
    class Mod(val id: String, var name: String, var game: Int, var enabled: Boolean) {
        val files = ArrayList<String>()      // installed files, relative to the game folder (as they are on the disk)
        val dirs = ArrayList<String>()       // folders that the mod created (removed when they are empty)
        val replaced = ArrayList<String>()   // files of the game that the mod replaced (kept in backup/<id>/)
    }

    private val lock = Any()
    private val manifest get() = File(root, "mods.txt")
    private fun backupDir(id: String) = File(File(root, "backup"), id)
    private fun zipOf(id: String) = File(root, "$id.zip")

    // ------------------------------------------------------------------ the list of the mods
    fun list(): List<Mod> = synchronized(lock) { load() }

    private fun load(): MutableList<Mod> {
        val out = ArrayList<Mod>()
        val f = manifest
        if (!f.isFile) return out
        var cur: Mod? = null
        for (l in f.readLines(Charsets.UTF_8)) {
            if (l.isEmpty()) continue
            val tag = l[0]
            val v = l.substring(1)
            when (tag) {
                'M' -> { val p = v.split('\t'); if (p.size >= 4) { cur = Mod(p[0], p[1], p[2].toIntOrNull() ?: 1, p[3] == "1"); out.add(cur) } }
                'f' -> cur?.files?.add(v)
                'd' -> cur?.dirs?.add(v)
                'r' -> cur?.replaced?.add(v)
            }
        }
        return out
    }

    private fun save(mods: List<Mod>) {
        root.mkdirs()
        val sb = StringBuilder()
        for (m in mods) {
            sb.append("M").append(m.id).append('\t').append(m.name.replace('\t', ' ').replace('\n', ' ')).append('\t').append(m.game).append('\t').append(if (m.enabled) "1" else "0").append('\n')
            for (x in m.files) sb.append('f').append(x).append('\n')
            for (x in m.dirs) sb.append('d').append(x).append('\n')
            for (x in m.replaced) sb.append('r').append(x).append('\n')
        }
        val tmp = File(root, "mods.txt.part")
        tmp.writeText(sb.toString(), Charsets.UTF_8)
        if (!tmp.renameTo(manifest)) { manifest.delete(); tmp.renameTo(manifest) }
    }

    // ------------------------------------------------------------------ import / switch
    /** unpacks a zip (a stream that can be opened twice) into the folder of the game and binds the mod to it */
    fun importZip(game: Int, fileName: String, open: () -> InputStream?): Boolean = jobs.job("Importing mod…") {
        val dir = gameDir(game) ?: throw RuntimeException("Gothic $game was not found on the phone")
        val name = fileName.removeSuffix(".zip").removeSuffix(".ZIP").ifBlank { "mod" }
        val id = idOf(name)
        val strip = detectStrip(open)   // a zip that is not a Gothic mod fails here, before anything is changed
        val mod: Mod
        synchronized(lock) {
            val all = load()
            val old = all.firstOrNull { it.id == id }
            if (old != null && old.game != game) throw RuntimeException("\"$name\" is already bound to Gothic ${old.game}")
            mod = old ?: Mod(id, name, game, true).also { all.add(it) }
            if (old != null && !old.enabled) zipOf(id).delete()   // a fresh copy comes instead of the packed one
            mod.enabled = true
            save(all)
        }
        installInto(mod, dir, strip, open)
    }

    fun setEnabled(id: String, on: Boolean): Boolean = jobs.job(if (on) "Unpacking mod…" else "Packing mod…") {
        val mod = synchronized(lock) { load().firstOrNull { it.id == id } } ?: throw RuntimeException("The mod is not in the list")
        val dir = gameDir(mod.game) ?: throw RuntimeException("Gothic ${mod.game} was not found on the phone")
        if (on == mod.enabled) return@job
        if (on) {
            val zip = zipOf(id)
            if (!zip.isFile) throw RuntimeException("The packed mod is missing: ${zip.name}")
            val strip = detectStrip { zip.inputStream() }
            mod.enabled = true
            installInto(mod, dir, strip) { zip.inputStream() }
            if (mod.files.isNotEmpty()) zip.delete()
        } else {
            uninstall(mod, dir)
        }
    }

    private fun idOf(name: String): String = name.lowercase().replace(Regex("[^a-z0-9._-]+"), "_").trim('_', '.').ifEmpty { "mod" }

    // ------------------------------------------------------------------ unpack
    private val roots = setOf("data", "system", "_work")
    private fun segsOf(name: String) = name.replace('\\', '/').split('/').filter { it.isNotEmpty() && it != "." }

    private fun checkCancel() { if (jobs.cancelled) throw RuntimeException("Cancelled") }

    /** how many leading folders of the entries are not part of the game folder (a zip with an extra top folder) */
    private fun detectStrip(open: () -> InputStream?): Int {
        val counts = HashMap<Int, Int>()
        jobs.note("Reading the zip…")
        (open() ?: throw RuntimeException("Cannot read the zip")).use { raw ->
            ZipInputStream(BufferedInputStream(raw, 1 shl 18)).use { z ->
                while (true) {
                    val e = z.nextEntry ?: break
                    if (!e.isDirectory) {
                        val s = segsOf(e.name)
                        val i = s.indexOfFirst { it.lowercase() in roots }
                        if (i >= 0 && i < s.size - 1) counts[i] = (counts[i] ?: 0) + 1
                    }
                    z.closeEntry()
                    checkCancel()
                }
            }
        }
        return counts.maxByOrNull { it.value }?.key ?: throw RuntimeException("No Data / system / _work folders in the zip: it is not a Gothic mod")
    }

    private fun installInto(mod: Mod, dir: File, strip: Int, open: () -> InputStream?) {
        val files = LinkedHashSet(mod.files)
        val dirs = LinkedHashSet(mod.dirs)
        val replaced = LinkedHashSet(mod.replaced)
        val bdir = backupDir(mod.id)
        try {
            (open() ?: throw RuntimeException("Cannot read the zip")).use { raw ->
                ZipInputStream(BufferedInputStream(raw, 1 shl 18)).use { z ->
                    while (true) {
                        val e = z.nextEntry ?: break
                        checkCancel()
                        val s = segsOf(e.name)
                        // only the folders of the game; never out of it
                        if (e.isDirectory || s.size <= strip + 1 || s.any { it == ".." } || s[strip].lowercase() !in roots) { z.closeEntry(); continue }
                        val rel = s.drop(strip)
                        var cur = dir
                        val resolved = ArrayList<String>()
                        for ((i, seg) in rel.withIndex()) {
                            val f = cur.listFiles()?.firstOrNull { it.name.equals(seg, ignoreCase = true) } ?: File(cur, seg)
                            if (i < rel.size - 1) {
                                if (f.exists() && !f.isDirectory) throw RuntimeException("${f.name} is a file, a folder is needed")
                                if (!f.exists()) {
                                    if (!f.mkdirs()) throw RuntimeException("Cannot create ${f.name}")
                                    dirs.add((resolved + f.name).joinToString("/"))
                                }
                            }
                            resolved.add(f.name)
                            cur = f
                        }
                        val relPath = resolved.joinToString("/")
                        if (cur.isDirectory) throw RuntimeException("${cur.name} is a folder, a file is needed")
                        // a file of the game (or of another mod) that is replaced: kept, it comes back when the mod is switched off
                        if (cur.exists() && relPath !in files) {
                            val b = File(bdir, relPath)
                            b.parentFile?.mkdirs()
                            if (!cur.renameTo(b)) { cur.copyTo(b, overwrite = true); cur.delete() }
                            replaced.add(relPath)
                        }
                        jobs.note(rel.last())
                        val tmp = File(cur.parentFile, cur.name + ".ogpart")
                        try {
                            tmp.outputStream().use { o ->
                                val buf = ByteArray(1 shl 18)
                                while (true) { val n = z.read(buf); if (n < 0) break; o.write(buf, 0, n); jobs.addDone(n.toLong()); checkCancel() }
                            }
                            if (cur.exists()) cur.delete()
                            if (!tmp.renameTo(cur)) throw RuntimeException("Cannot write ${cur.name}")
                        } finally { if (tmp.exists()) tmp.delete() }
                        files.add(relPath)
                        z.closeEntry()
                    }
                }
            }
        } finally {
            // what is on the disk is recorded even when the job failed or was cancelled: the mod can be switched off to remove it
            synchronized(lock) {
                val all = load()
                val m = all.firstOrNull { it.id == mod.id } ?: Mod(mod.id, mod.name, mod.game, true).also { all.add(it) }
                m.enabled = true
                m.files.clear(); m.files.addAll(files)
                m.dirs.clear(); m.dirs.addAll(dirs)
                m.replaced.clear(); m.replaced.addAll(replaced)
                save(all)
                mod.files.clear(); mod.files.addAll(files)
            }
        }
    }

    // ------------------------------------------------------------------ pack
    private fun uninstall(mod: Mod, dir: File) {
        root.mkdirs()
        val zip = zipOf(mod.id)
        val tmp = File(root, mod.id + ".zip.part")
        try {
            ZipOutputStream(BufferedOutputStream(FileOutputStream(tmp), 1 shl 18)).use { z ->
                z.setLevel(Deflater.NO_COMPRESSION)   // the archives of a mod are packed already: fast, nothing is gained
                for (rel in mod.files) {
                    checkCancel()
                    val f = File(dir, rel)
                    if (!f.isFile) continue
                    jobs.note(f.name)
                    z.putNextEntry(ZipEntry(rel))
                    f.inputStream().use { i ->
                        val buf = ByteArray(1 shl 18)
                        while (true) { val n = i.read(buf); if (n < 0) break; z.write(buf, 0, n); jobs.addDone(n.toLong()); checkCancel() }
                    }
                    z.closeEntry()
                }
            }
            if (zip.exists()) zip.delete()
            if (!tmp.renameTo(zip)) throw RuntimeException("Cannot write ${zip.name}")
        } finally { if (tmp.exists()) tmp.delete() }

        removeFiles(mod, dir)
        synchronized(lock) {
            val all = load()
            val m = all.firstOrNull { it.id == mod.id }
            if (m != null) { m.enabled = false; m.files.clear(); m.dirs.clear(); m.replaced.clear(); save(all) }
        }
    }

    /** deletes the mod for good: its files leave the game (the replaced ones come back), the packed zip and the entry go away */
    fun remove(id: String): Boolean = jobs.job("Deleting mod…") {
        val mod = synchronized(lock) { load().firstOrNull { it.id == id } } ?: throw RuntimeException("The mod is not in the list")
        if (mod.enabled && mod.files.isNotEmpty()) {
            val dir = gameDir(mod.game) ?: throw RuntimeException("Gothic ${mod.game} was not found on the phone")
            removeFiles(mod, dir)
        }
        zipOf(id).delete()
        backupDir(id).deleteRecursively()
        synchronized(lock) { val all = load(); all.removeAll { it.id == id }; save(all) }
    }

    private fun removeFiles(mod: Mod, dir: File) {
        jobs.note("Removing the files…")
        for (rel in mod.files) File(dir, rel).delete()
        val bdir = backupDir(mod.id)
        for (rel in mod.replaced) {
            val b = File(bdir, rel)
            if (!b.isFile) continue
            val t = File(dir, rel)
            t.parentFile?.mkdirs()
            if (!b.renameTo(t)) { b.copyTo(t, overwrite = true); b.delete() }
        }
        bdir.deleteRecursively()
        for (d in mod.dirs.sortedByDescending { it.count { c -> c == '/' } }) {
            val f = File(dir, d)
            if (f.isDirectory && (f.list()?.isEmpty() == true)) f.delete()
        }
    }
}
