package com.opengothic

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.util.TypedValue
import android.annotation.SuppressLint
import android.view.View
import android.widget.Toast
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.webkit.WebViewClient
import org.json.JSONArray
import org.json.JSONObject
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.io.File

class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "OpenGothic"
        private const val REQUEST_STORAGE_PERMISSION = 1001

        init {
            try {
                System.loadLibrary("c++_shared")
            } catch (e: UnsatisfiedLinkError) {
                Log.w(TAG, "c++_shared: ${e.message}")
            }
            try {
                System.loadLibrary("opengothic")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "opengothic: ${e.message}")
            }
        }
    }

    private external fun nativeGetLastLog(): String
    private external fun nativeIsGameRunning(): Boolean

    private var webView: WebView? = null
    private var detectedGameDir: String = "/sdcard/Gothic2"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "MainActivity (Launcher) onCreate")

        checkAndRequestStoragePermissions()
        val shortcutGame = intent.getIntExtra("game", 0)
        if (shortcutGame != 0) { scanned = scan(); rememberScan(); scanning = false }
        else Thread {
            // the scan walks the storage: the menu is shown first and updated when it is done
            scanned = scan()
            rememberScan()
            scanning = false
            runOnUiThread { webView?.evaluateJavascript("window.onResumed && onResumed()", null) }
        }.start()
        detectedGameDir = findGameDirectory()

        setContentView(createUiView())
        // back: the page closes its editor / window / tab first (onBack returns true), then the app goes to the background
        onBackPressedDispatcher.addCallback(this, object : androidx.activity.OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                val w = webView
                if (w == null) { finish(); return }
                w.evaluateJavascript("window.onBack ? onBack() : false") { r ->
                    if (r != "true") { isEnabled = false; onBackPressedDispatcher.onBackPressed(); isEnabled = true }
                }
            }
        })
        // a home screen icon of a game: start it at once
        if (shortcutGame != 0) {
            intent.removeExtra("game")
            if (gamePath(shortcutGame) != null) launchGame(shortcutGame)
        }
        // adb: am start -n com.gothic.droid/com.opengothic.MainActivity --ez autostart true (scripts/android_*.sh)
        if (intent.getBooleanExtra("autostart", false)) {
            intent.removeExtra("autostart")
            launchGame()
        }
    }

    override fun onResume() {
        super.onResume()
        webView?.evaluateJavascript("window.onResumed && onResumed()", null)
    }

    private fun checkAndRequestStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Log.i(TAG, "Requesting MANAGE_APP_ALL_FILES_ACCESS_PERMISSION")
                Toast.makeText(
                    this,
                    "All files access is required to launch Gothic",
                    Toast.LENGTH_LONG
                ).show()
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    intent.data = Uri.parse("package:$packageName")
                    startActivity(intent)
                } catch (e: Exception) {
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)
                }
            }
        } else {
            val readPerm = ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
            val writePerm = ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
            if (readPerm != PackageManager.PERMISSION_GRANTED || writePerm != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(
                    this,
                    arrayOf(
                        Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE
                    ),
                    REQUEST_STORAGE_PERMISSION
                )
            }
        }
    }

    // ---- the games on the phone. A game is a folder with Data/*.vdf; which one it is (1 or 2) follows from
    // the files the engine itself tells them apart by (OU.DAT: Gothic 2, OU.BIN: Gothic 1), then the addon archives
    // and the exe, then the name of the folder
    private data class Game(val id: Int, val title: String)
    private val gameList = listOf(Game(1, "Gothic 1"), Game(2, "Gothic 2"), Game(3, "Archolos"))
    @Volatile private var scanning = true

    private fun child(dir: File, name: String): File? {
        File(dir, name).let { if (it.exists()) return it }
        return dir.listFiles()?.firstOrNull { it.name.equals(name, ignoreCase = true) }
    }

    private fun path(dir: File, vararg parts: String): File? {
        var d: File = dir
        for (p in parts) d = child(d, p) ?: return null
        return d
    }

    private fun vdfs(dir: File): List<File> {
        val data = child(dir, "Data") ?: return emptyList()
        return data.listFiles { f -> f.name.endsWith(".vdf", ignoreCase = true) }?.toList() ?: emptyList()
    }

    private fun hasVdf(dir: File) = dir.isDirectory && vdfs(dir).isNotEmpty()

    // 1, 2, or 0 when the folder gives no hint. The same marks as the engine's own detection (Gothic::detectGothicVersion):
    // the keys only the Gothic 2 Gothic.ini has, OU.DAT; OU.BIN is in Gothic 1 and in some translated Gothic 2 (it is not enough)
    private fun versionOf(dir: File): Int {
        var g1 = 0
        var g2 = 0
        val cut = arrayOf("_work", "Data", "Scripts", "content", "CUTSCENE")
        if (path(dir, *cut, "OU.BIN") != null) g1++
        if (path(dir, *cut, "OU.DAT") != null) g2++
        val ini = path(dir, "system", "Gothic.ini")
        if (ini != null) {
            val t = try { ini.readText(Charsets.ISO_8859_1) } catch (e: Exception) { "" }
            for (k in arrayOf("KEYSDEFAULT1", "PATCHVERSION", "useGothic1Controls"))
                if (t.contains(k, ignoreCase = true)) g2++
        }
        if (path(dir, "system", "Gothic2.exe") != null || path(dir, "Gothic2.exe") != null) g2 += 2
        if (vdfs(dir).any { it.name.contains("addon", ignoreCase = true) }) g2 += 2
        if (path(dir, "system", "GothicMod.exe") != null || path(dir, "system", "Gothic.exe") != null) g1++
        if (g1 != g2) return if (g1 > g2) 1 else 2
        val n = dir.name.lowercase()
        if (Regex("(2|ii|raven|ворон|g2)").containsMatchIn(n)) return 2
        if (n.contains("gothic") || n.contains("готик") || Regex("(1|g1)").containsMatchIn(n)) return 1
        return 0
    }

    // the ini of the Archolos mod in a game folder (relative to it) or null. The engine starts a mod with -game:<ini>
    private fun modIni(dir: File): String? {
        val sys = child(dir, "system") ?: return null
        val ini = sys.listFiles { f -> f.name.endsWith(".ini", ignoreCase = true) &&
            (f.name.contains("archolos", true) || f.name.contains("myrtana", true)) }?.firstOrNull()
        if (ini != null) return "system/" + ini.name
        // only the archives: the mod's own ini is made then
        val mods = child(dir, "Data")?.let { child(it, "ModVDF") }?.listFiles { f -> f.name.contains("archolos", true) || f.name.contains("myrtana", true) }
        return if (mods != null && mods.isNotEmpty()) "system/Archolos.ini" else null
    }

    private val skipDirs = setOf("android", "dcim", "pictures", "movies", "music", "alarms", "notifications", "podcasts",
        "ringtones", "audiobooks", "documents", "recordings", "screenshots", "telegram", "whatsapp", "data", "_work", "miles", "saves")

    // every storage volume: the internal one, sd cards, usb drives
    private fun roots(): List<File> {
        val out = LinkedHashSet<File>()
        out.add(Environment.getExternalStorageDirectory())
        File("/storage").listFiles()?.forEach { if (it.name != "self" && it.name != "emulated" && it.isDirectory) out.add(it) }
        getExternalFilesDirs(null).forEach { d ->
            // /storage/XXXX/Android/data/<pkg>/files -> the root of the volume
            val p = d?.absolutePath ?: return@forEach
            val i = p.indexOf("/Android/")
            if (i > 0) out.add(File(p.substring(0, i)))
        }
        return out.toList()
    }

    // folders to the depth of 4 under the roots, the ones named like a game first
    private fun scan(): Map<Int, String> {
        val found = LinkedHashMap<Int, String>()
        fun consider(dir: File) {
            val v = versionOf(dir)
            if (v != 0 && v !in found) found[v] = dir.path
            // Chronicles of Myrtana: Archolos is a mod of Gothic 2: its ini and archives lie in the game folder
            if (3 !in found && modIni(dir) != null) found[3] = dir.path
        }
        // the app's own folder: games put there by the downloader
        getExternalFilesDir(null)?.let { base ->
            for (g in gameList) File(base, "Gothic${g.id}").let { if (hasVdf(it)) consider(it) }
        }
        val queue = ArrayDeque<Pair<File, Int>>()
        for (r in roots()) queue.add(r to 0)
        var visited = 0
        while (queue.isNotEmpty() && visited < 6000) {
            val (dir, depth) = queue.removeFirst()
            visited++
            if (hasVdf(dir)) { consider(dir); continue }
            if (depth >= 4) continue
            val kids = dir.listFiles { f -> f.isDirectory && !f.name.startsWith(".") && f.name.lowercase() !in skipDirs } ?: continue
            // the likely ones first
            kids.sortedByDescending { Regex("gothic|готик|g1|g2|games|игры|download", RegexOption.IGNORE_CASE).containsMatchIn(it.name) }
                .forEach { queue.add(it to depth + 1) }
        }
        return found
    }

    private fun prefs() = getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE)

    private var scanned: Map<Int, String> = emptyMap()
    private var dl: PackDownloader? = null
    private var dlGame = 0

    // the folder of a game: the one the player chose, or the one found on the phone
    private fun gamePath(id: Int): String? {
        prefs().getString("path_$id", null)?.let { if (hasVdf(File(it))) return it }
        return scanned[id]
    }

    // a folder that was found is kept: the next start uses it without searching, until it is gone
    private fun rememberScan() {
        val e = prefs().edit()
        for ((id, p) in scanned) {
            val old = prefs().getString("path_$id", null)
            if (old == null || !hasVdf(File(old))) e.putString("path_$id", p)
        }
        e.apply()
    }

    // a game that this app downloaded (its own folder, with the mark of the language packs) can be deleted by the app
    private fun appCopy(id: Int) = File(getExternalFilesDir(null), "Gothic$id")

    private fun isDownloaded(id: Int): Boolean {
        val dir = appCopy(id)
        return gamePath(id) == dir.path && File(dir, ".og_langs.json").exists()
    }

    private fun selectedGame(): Int = prefs().getInt("sel", 1)

    private fun findGameDirectory(): String = gamePath(selectedGame()) ?: "/sdcard/Gothic${selectedGame()}"

    // a folder chosen with the system picker -> its path
    private fun pathOfTree(uri: Uri): String? {
        val id = android.provider.DocumentsContract.getTreeDocumentId(uri)
        val vol = id.substringBefore(':')
        val rel = id.substringAfter(':', "")
        val base = if (vol == "primary") Environment.getExternalStorageDirectory().path else "/storage/$vol"
        return if (rel.isEmpty()) base else "$base/$rel"
    }

    private var pickFor = 1
    private val picker = registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.OpenDocumentTree()) { uri ->
        val p = uri?.let { pathOfTree(it) }
        when {
            p == null -> {}
            !hasVdf(File(p)) -> Toast.makeText(this, "No game files (Data/*.vdf) in $p", Toast.LENGTH_LONG).show()
            else -> prefs().edit().putString("path_$pickFor", p).apply()
        }
        webView?.evaluateJavascript("window.onResumed && onResumed()", null)
    }

    // big logs hang the ui thread in a TextView: only the end of the file is shown
    private fun readTail(f: File, max: Int = 20000): String {
        val t = f.readText()
        return if (t.length > max) "...\n" + t.substring(t.length - max) else t
    }

    private fun readCompleteLog(): String {
        val sb = StringBuilder()

        // 1. Check crash.log in game directory
        val crashFile = File(detectedGameDir, "crash.log")
        if (crashFile.exists()) {
            sb.append("=== CRASH LOG (/sdcard/Gothic2/crash.log) ===\n")
            sb.append(readTail(crashFile))
            sb.append("\n\n")
        }

        // 2. Check Java crash log
        val javaCrashFile = File(filesDir, "last_crash.txt")
        if (javaCrashFile.exists()) {
            sb.append("=== JAVA CRASH LOG ===\n")
            sb.append(readTail(javaCrashFile))
            sb.append("\n\n")
        }

        // 3. Read native in-memory log
        try {
            val nativeLog = nativeGetLastLog()
            if (nativeLog.isNotBlank()) {
                sb.append("=== NATIVE LOG ===\n")
                sb.append(if (nativeLog.length > 20000) nativeLog.substring(nativeLog.length - 20000) else nativeLog)
                sb.append("\n\n")
            }
        } catch (e: Exception) {
            sb.append("nativeGetLastLog error: ${e.message}\n")
        }

        // 4. Check opengothic_log.txt file
        val sessionLogFile = File(detectedGameDir, "opengothic_log.txt")
        if (sessionLogFile.exists() && sb.length < 500) {
            sb.append("=== LOG FILE ===\n")
            sb.append(readTail(sessionLogFile))
        }

        return if (sb.isNotBlank()) sb.toString() else "The log is empty. Run the game to collect logs."
    }

    private fun copyLogToClipboard() {
        val logText = readCompleteLog()
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("GothicDroid Log", logText)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, "Log copied to the clipboard! (${logText.length} chars)", Toast.LENGTH_SHORT).show()
    }

    private fun launchGame(id: Int = selectedGame()) {
        prefs().edit().putInt("sel", id).apply()
        detectedGameDir = findGameDirectory()
        val intent = Intent(this, GameActivity::class.java).apply {
            putExtra(GameActivity.EXTRA_GAME_DIR, detectedGameDir)
            if (id == 3) putExtra(GameActivity.EXTRA_MOD, modIni(File(detectedGameDir)) ?: "")
        }
        startActivity(intent)
    }

    // ---- the settings of the web page (web/shell.html): the same names and the same Gothic.ini keys as the engine
    // (og_setting_get / og_setting_set in game/mainwindow.cpp), here written to the game folder before the engine runs
    private data class Setting(val sec: String, val key: String, val def: Int, val real: Boolean = false)

    private val settings = mapOf(
        "fpsLimit" to Setting("INTERNAL", "webFpsLimit", 0),
        "resScale" to Setting("INTERNAL", "vidResIndex", 0, true),
        "shadows" to Setting("INTERNAL", "webShadows", 1),
        "drawDist" to Setting("INTERNAL", "webDrawDist", 0),
        "worldDist" to Setting("INTERNAL", "webWorldDistM", 0),
        "skipFarAnim" to Setting("INTERNAL", "webSkipFarAnim", 0),
        "reflections" to Setting("ENGINE", "zEnvMappingEnabled", 0),
        "ssao" to Setting("ENGINE", "zCloudShadowScale", 0),
        "texQuality" to Setting("INTERNAL", "webTexQuality", 0),
        "waterQuality" to Setting("INTERNAL", "webWaterQuality", 0),
        "waterPhysics" to Setting("INTERNAL", "webWaterPhysics", 0),
        "blood" to Setting("INTERNAL", "webBlood", 0),
        "fog" to Setting("INTERNAL", "webFog", 0),
        "sun" to Setting("INTERNAL", "webSun", 0),
        "barrier" to Setting("INTERNAL", "webBarrier", 0),
        "barrierVol" to Setting("INTERNAL", "webBarrierVol", 100),
        "barrierZoneShow" to Setting("INTERNAL", "webBarrierZoneShow", 0)
    )

    private fun iniFile() = File(detectedGameDir, "Gothic.ini")

    private fun iniGet(s: Setting): Int {
        var sec = ""
        try {
            for (raw in iniFile().readLines()) {
                val line = raw.trim()
                if (line.startsWith("[") && line.endsWith("]")) { sec = line.substring(1, line.length - 1); continue }
                if (sec != s.sec) continue
                val k = line.substringBefore('=', "").trim()
                if (k == s.key) return line.substringAfter('=').trim().toDoubleOrNull()?.toInt() ?: s.def
            }
        } catch (e: Exception) {}
        return s.def
    }

    private fun iniSet(s: Setting, v: Int) {
        try {
            val f = iniFile()
            val lines = if (f.exists()) f.readLines().toMutableList() else mutableListOf()
            val text = if (s.real) String.format(java.util.Locale.US, "%f", v.toFloat()) else v.toString()
            var sec = ""
            var secAt = -1
            for (i in lines.indices) {
                val line = lines[i].trim()
                if (line.startsWith("[") && line.endsWith("]")) {
                    sec = line.substring(1, line.length - 1)
                    if (sec == s.sec) secAt = i
                    continue
                }
                if (sec == s.sec && line.substringBefore('=', "").trim() == s.key) { lines[i] = "${s.key}=$text"; f.writeText(lines.joinToString("\n") + "\n"); return }
            }
            if (secAt < 0) { lines.add("[${s.sec}]"); lines.add("${s.key}=$text") }
            else lines.add(secAt + 1, "${s.key}=$text")
            f.writeText(lines.joinToString("\n") + "\n")
        } catch (e: Exception) {
            Log.e(TAG, "ini write error: ${e.message}")
        }
    }

    inner class LauncherBridge {
        // [{id, title, path|null, vdf, version}] for both games; the scan runs again on `rescan`
        @JavascriptInterface
        fun games(): String {
            val a = JSONArray()
            for (g in gameList) {
                val p = gamePath(g.id)
                val o = JSONObject().put("id", g.id).put("title", g.title).put("path", p ?: JSONObject.NULL)
                    .put("canDownload", g.id != 3)
                    .put("downloaded", isDownloaded(g.id))
                if (p != null) o.put("vdf", vdfs(File(p)).size).put("manual", prefs().getString("path_${g.id}", null) == p)
                a.put(o)
            }
            return a.toString()
        }

        @JavascriptInterface
        fun scanning(): Boolean = scanning

        @JavascriptInterface
        fun rescan(): String { scanning = true; scanned = scan(); rememberScan(); scanning = false; return games() }

        @JavascriptInterface
        fun pick(id: Int) { pickFor = id; runOnUiThread { picker.launch(null) } }

        @JavascriptInterface
        fun forget(id: Int) { prefs().edit().remove("path_$id").apply() }

        // the game whose settings the page shows
        @JavascriptInterface
        fun selected(): Int = selectedGame()

        @JavascriptInterface
        fun select(id: Int) { prefs().edit().putInt("sel", id).apply(); detectedGameDir = findGameDirectory() }

        @JavascriptInterface
        fun openUrl(url: String) {
            if (!url.startsWith("https://")) return
            runOnUiThread { try { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) } catch (e: Exception) { Log.e(TAG, "openUrl: ${e.message}") } }
        }

        // the donate window: a wallet address goes to the system clipboard
        @JavascriptInterface
        fun copyText(text: String) {
            if (text.length > 200) return
            runOnUiThread {
                try {
                    val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                    cm.setPrimaryClip(ClipData.newPlainText("address", text))
                } catch (e: Exception) { Log.e(TAG, "copyText: ${e.message}") }
            }
        }

        @JavascriptInterface
        fun get(name: String): Int { detectedGameDir = findGameDirectory(); return settings[name]?.let { iniGet(it) } ?: -1 }

        @JavascriptInterface
        fun set(name: String, v: Int): Int { val s = settings[name] ?: return 0; detectedGameDir = findGameDirectory(); iniSet(s, v); return 1 }

        // the touch controls (Controls tab): {sens, opacity, items:{key:{x,y,k}}}. The game process reads the same file
        @JavascriptInterface
        fun controls(): String = try { File(filesDir, "controls.json").readText() } catch (e: Exception) { "{}" }

        @JavascriptInterface
        fun setControls(json: String) {
            try { JSONObject(json); File(filesDir, "controls.json").writeText(json) } catch (e: Exception) { Log.e(TAG, "controls: ${e.message}") }
        }

        // the layout editor is sideways and without the system bars; the menu goes back to the phone's own orientation
        @JavascriptInterface
        fun editor(on: Boolean) {
            runOnUiThread {
                requestedOrientation = if (on) android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                                       else android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                val c = androidx.core.view.WindowInsetsControllerCompat(window, window.decorView)
                if (on) c.hide(androidx.core.view.WindowInsetsCompat.Type.systemBars()) else c.show(androidx.core.view.WindowInsetsCompat.Type.systemBars())
                c.systemBarsBehavior = androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        }

        @JavascriptInterface
        fun version(): String = try { packageManager.getPackageInfo(packageName, 0).versionName ?: "" } catch (e: Exception) { "" }

        @JavascriptInterface
        fun log(): String = readCompleteLog()

        @JavascriptInterface
        fun copyLog() { runOnUiThread { copyLogToClipboard() } }

        @JavascriptInterface
        fun start(id: Int) { runOnUiThread { launchGame(id) } }

        // ---- the link to release.json of the site (the same manifest as the browser version) and the download.
        // A link that does not end with .json is the server of the PC (web/serve.py), see PackDownloader.
        private val defaultServer = "https://gothicplay.fun/release.json"
        private val oldDefaultServer = "http://192.168.0.100:8322/"   // the default of the earlier versions: now the site

        @JavascriptInterface
        fun server(): String {
            val s = prefs().getString("server", defaultServer) ?: defaultServer
            return if (s == oldDefaultServer) defaultServer else s
        }

        @JavascriptInterface
        fun resetServer(): String { prefs().edit().remove("server").apply(); return defaultServer }

        @JavascriptInterface
        fun setServer(url: String) { prefs().edit().putString("server", url.trim()).apply() }

        // the languages and the sizes of a game on the server: onIndex(id, {error|base, langs:[{id,name,text,voice,hasVoice}]})
        @JavascriptInterface
        fun fetchIndex(id: Int) {
            Thread {
                val o = JSONObject()
                try {
                    val i = PackDownloader(server()).index(id)
                    o.put("base", i.baseSize)
                    val a = JSONArray()
                    for (l in i.langs) a.put(JSONObject().put("id", l.id).put("name", l.name).put("text", l.textSize).put("voice", l.voiceSize).put("hasVoice", l.hasVoice))
                    o.put("langs", a)
                } catch (e: Exception) {
                    o.put("error", e.message ?: e.toString())
                }
                runOnUiThread { webView?.evaluateJavascript("window.onIndex && onIndex($id, ${o})", null) }
            }.start()
        }

        // deletes the copy the app downloaded; the page asks the player first
        @JavascriptInterface
        fun deleteGame(id: Int) {
            if (dl?.running == true || !isDownloaded(id)) return
            Thread {
                appCopy(id).deleteRecursively()
                prefs().edit().remove("path_$id").apply()
                scanned = scanned - id
                runOnUiThread { webView?.evaluateJavascript("window.onResumed && onResumed()", null) }
            }.start()
        }

        @JavascriptInterface
        fun download(id: Int, text: String, voice: String): Boolean {
            if (dl?.running == true) return false
            val d = PackDownloader(server())
            dl = d
            dlGame = id
            val target = File(getExternalFilesDir(null), "Gothic$id")
            runOnUiThread { window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
            Thread {
                d.install(id, text, voice, target)
                runOnUiThread { window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
                if (d.finished) {
                    // the game is in the folder of the app now: the next start uses it
                    prefs().edit().putString("path_$id", target.path).apply()
                    scanned = scanned + (id to target.path)
                }
                runOnUiThread { webView?.evaluateJavascript("window.onResumed && onResumed()", null) }
            }.start()
            return true
        }

        @JavascriptInterface
        fun dlState(): String {
            val d = dl ?: return JSONObject().put("running", false).toString()
            return JSONObject().put("game", dlGame).put("running", d.running).put("finished", d.finished).put("error", d.error)
                .put("phase", d.phase).put("done", d.done.get()).put("total", d.total).toString()
        }

        @JavascriptInterface
        fun cancelDownload() { dl?.cancel() }

        // the languages installed in the folder of a game: {text, voice} or "" (a game put there by hand)
        @JavascriptInterface
        fun langs(id: Int): String {
            val p = gamePath(id) ?: return ""
            return try { File(p, ".og_langs.json").readText().let { JSONObject(it) }.let { JSONObject().put("text", it.getString("text")).put("voice", it.getString("voice")).toString() } } catch (e: Exception) { "" }
        }

        @JavascriptInterface
        fun pin(id: Int): Boolean = pinShortcut(id)
    }

    // an icon of the game on the home screen: it starts the game without the menu
    private fun pinShortcut(id: Int): Boolean {
        val sm = getSystemService(android.content.pm.ShortcutManager::class.java)
        if (sm == null || !sm.isRequestPinShortcutSupported) {
            Toast.makeText(this, "The launcher does not support pinned shortcuts", Toast.LENGTH_LONG).show()
            return false
        }
        val bmp = try {
            assets.open(if (id == 3) "img/myrtana.jpg" else "img/icon_g$id.png").use { android.graphics.BitmapFactory.decodeStream(it) }
        } catch (e: Exception) { null }
        val icon = if (bmp != null) {
            // the icon of the exe of the game, on a dark tile (adaptive icons crop the edges)
            val size = 432
            val tile = android.graphics.Bitmap.createBitmap(size, size, android.graphics.Bitmap.Config.ARGB_8888)
            val c = android.graphics.Canvas(tile)
            c.drawColor(Color.parseColor("#1b1b1b"))
            val p = android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG)
            if (id == 3) {
                // a picture (no exe icon): the middle square fills the tile
                val side = minOf(bmp.width, bmp.height)
                val src = android.graphics.Rect((bmp.width - side) / 2, (bmp.height - side) / 2, (bmp.width + side) / 2, (bmp.height + side) / 2)
                c.drawBitmap(bmp, src, android.graphics.RectF(size * 0.15f, size * 0.15f, size * 0.85f, size * 0.85f), p)
            } else {
                val box = size * 0.5f
                c.drawBitmap(bmp, null, android.graphics.RectF((size - box) / 2, (size - box) / 2, (size + box) / 2, (size + box) / 2), p)
            }
            android.graphics.drawable.Icon.createWithAdaptiveBitmap(tile)
        } else android.graphics.drawable.Icon.createWithResource(this, android.R.drawable.sym_def_app_icon)
        val intent = Intent(this, MainActivity::class.java).apply {
            action = Intent.ACTION_MAIN
            putExtra("game", id)
        }
        val info = android.content.pm.ShortcutInfo.Builder(this, "game$id")
            .setShortLabel(gameList.first { it.id == id }.title)
            .setLongLabel(gameList.first { it.id == id }.let { if (it.id == 3) "Chronicles of Myrtana: Archolos" else it.title })
            .setIcon(icon)
            .setIntent(intent)
            .build()
        return sm.requestPinShortcut(info, null)
    }

    @SuppressLint("SetJavaScriptEnabled", "AddJavascriptInterface")
    private fun createUiView(): View {
        val wv = WebView(this)
        wv.setBackgroundColor(Color.parseColor("#151515"))
        wv.settings.javaScriptEnabled = true
        wv.settings.domStorageEnabled = true
        wv.addJavascriptInterface(LauncherBridge(), "Launcher")
        wv.webViewClient = WebViewClient()
        wv.loadUrl("file:///android_asset/launcher.html")
        webView = wv
        return wv
    }
}
