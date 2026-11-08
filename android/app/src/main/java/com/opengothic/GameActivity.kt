package com.opengothic

import android.content.Intent
import android.content.pm.ActivityInfo
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.annotation.SuppressLint
import android.graphics.Color
import android.view.*
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import org.json.JSONArray
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File

class GameActivity : AppCompatActivity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "OpenGothic"
        const val EXTRA_GAME_DIR = "extra_game_dir"
        const val EXTRA_SKY_DIR = "extra_sky_dir"   // folder with the sky pack (g1, g2) for the sky setting
        const val AUTOSAVE_SLOT = 99               // save_slot_99.sav: the hidden slot of MainWindow::autoSave
        const val RESUME_MARK = ".og_resume"       // in the game folder: the game was saved on onStop and not closed since
        const val EXTRA_MOD = "extra_mod"   // the ini of a mod in the game folder (Archolos), "" - none
        const val EXTRA_GAME_VERSION = "extra_game_version"   // 1 or 2 (-g1/-g2 of the engine), 0 - the engine guesses

        init {
            // the game runs in its own process (":game"): the WebView there needs its own data directory
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                try { WebView.setDataDirectorySuffix("game") } catch (e: Exception) { Log.w(TAG, "WebView suffix: ${e.message}") }
            }
            try {
                System.loadLibrary("c++_shared")
            } catch (e: UnsatisfiedLinkError) {
                Log.w(TAG, "c++_shared: ${e.message}")
            }
            try {
                System.loadLibrary("opengothic")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Failed to load opengothic: ${e.message}")
            }
        }
    }

    private external fun nativeSetSurface(surface: Surface, gameDir: String)
    private external fun nativeSetSkyDir(dir: String)
    private external fun nativeSetMod(mod: String)
    private external fun nativeSetGameVersion(version: Int)
    private external fun nativeSurfaceChanged(width: Int, height: Int)
    private external fun nativeSetDensity(density: Float)
    private external fun nativeDestroySurface()
    private external fun nativeOnTouch(action: Int, x: Float, y: Float, id: Int)
    private external fun nativeOnKey(action: Int, keyCode: Int)
    private external fun nativeIsGameRunning(): Boolean
    private external fun nativeExitRequested(): Boolean
    private external fun nativeGetLastLog(): String

    private external fun nativeSettingGet(name: String): Int
    private external fun nativeSettingSet(name: String, value: Int): Int
    private external fun nativeBarrierZone(kind: Int): Int
    private external fun nativeUiState(): Int
    private external fun nativeFrames(): Int
    private external fun nativePerfInfo(): String
    private external fun nativeFrameTimes(): String
    private external fun nativeQuick(kind: Int)
    private external fun nativeSetLoadSlot(slot: Int)
    private external fun nativeSetRenderer(gl: Boolean)
    private external fun nativeAutoSave(): Int
    private external fun nativeSaveBusy(): Int
    private external fun nativeQuests(): String
    private external fun nativeVideoTake(): String
    private external fun nativeVideoDone()
    private external fun nativeQuickSlots(): String
    private external fun nativePotSlots(): String
    private external fun nativeQuickPanel(open: Int, first: Int)
    private external fun nativePointer(type: Int, x: Int, y: Int)
    private external fun nativeLook(dx: Int, dy: Int)
    private external fun nativeZoom(steps: Int)
    private external fun nativeWebKey(action: Int, code: String)

    private lateinit var surfaceView: SurfaceView
    private var webView: WebView? = null
    private var surfaceHolder: SurfaceHolder? = null
    private var gameDir: String = "/sdcard/Gothic2"
    private var switching = false // the renderer is being switched: saved already, no second autosave in onStop

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "GameActivity onCreate")

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        // the on-screen keyboard (savegame name) goes over the game, the touch controls stay where they are
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        hideSystemBars()

        gameDir = intent.getStringExtra(EXTRA_GAME_DIR) ?: "/sdcard/Gothic2"
        nativeSetMod(intent.getStringExtra(EXTRA_MOD) ?: "")
        nativeSetGameVersion(intent.getIntExtra(EXTRA_GAME_VERSION, 0))
        nativeSetSkyDir(intent.getStringExtra(EXTRA_SKY_DIR) ?: "")
        nativeSetRenderer(RendererPref.isGl(this))
        // the process was killed in the background after an autosave (the marker is there): the game goes on from it.
        // The marker goes at once: a save that cannot be loaded must not make a loop of restarts
        val marker = File(gameDir, RESUME_MARK)
        if (marker.exists()) {
            marker.delete()
            if (File(gameDir, "save_slot_$AUTOSAVE_SLOT.sav").isFile) {
                Log.i(TAG, "the process was killed in the background: loading the autosave")
                nativeSetLoadSlot(AUTOSAVE_SLOT)
            }
        }

        // Global uncaught exception logger
        val defaultHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            val crashMsg = "=== JAVA CRASH IN GAMEACTIVITY ===\n" +
                    "Thread: ${thread.name}\n" +
                    Log.getStackTraceString(throwable)
            Log.e(TAG, crashMsg)
            try {
                val f = File(filesDir, "last_crash.txt")
                f.writeText(crashMsg)
                val sdF = File(gameDir, "crash.log")
                sdF.writeText(crashMsg)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to write crash log: ${e.message}")
            }
            defaultHandler?.uncaughtException(thread, throwable)
        }

        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        surfaceView.isFocusable = true
        surfaceView.isFocusableInTouchMode = true
        surfaceView.requestFocus()

        surfaceView.setOnTouchListener { _, event ->
            val action = event.actionMasked
            when (action) {
                MotionEvent.ACTION_MOVE -> {
                    for (i in 0 until event.pointerCount) {
                        nativeOnTouch(action, event.getX(i), event.getY(i), event.getPointerId(i))
                    }
                }
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    val idx = event.actionIndex
                    nativeOnTouch(MotionEvent.ACTION_DOWN, event.getX(idx), event.getY(idx), event.getPointerId(idx))
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                    val idx = event.actionIndex
                    nativeOnTouch(MotionEvent.ACTION_UP, event.getX(idx), event.getY(idx), event.getPointerId(idx))
                }
                MotionEvent.ACTION_CANCEL -> {
                    val idx = event.actionIndex
                    nativeOnTouch(MotionEvent.ACTION_UP, event.getX(idx), event.getY(idx), event.getPointerId(idx))
                }
            }
            true
        }

        // the back button / gesture (also the predictive one): the side menu
        onBackPressedDispatcher.addCallback(this, object : androidx.activity.OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = backPressed()
        })

        val root = FrameLayout(this)
        root.addView(surfaceView, FrameLayout.LayoutParams(-1, -1))
        root.addView(createUiView(), FrameLayout.LayoutParams(-1, -1))
        setContentView(root)
    }

    // The interface is the browser version's page (assets/ui.html, built from web/shell.html) over the game:
    // its calls into the engine (Module.ccall in the browser) come here
    inner class UiBridge {
        @JavascriptInterface
        fun call(name: String, argsJson: String): String {
            val a = JSONArray(argsJson)
            return when (name) {
                "og_setting_get" -> nativeSettingGet(a.getString(0)).toString()
                "og_setting_set" -> nativeSettingSet(a.getString(0), a.getInt(1)).toString()
                "og_barrier_zone" -> nativeBarrierZone(a.getInt(0)).toString()
                "og_ui_state" -> nativeUiState().toString()
                "og_frames" -> nativeFrames().toString()
                "og_perf_info" -> nativePerfInfo()
                "og_frame_times" -> nativeFrameTimes()
                "og_quick" -> { nativeQuick(a.getInt(0)); "0" }
                "og_quests" -> nativeQuests()
                "og_video_take" -> nativeVideoTake()
                "og_video_done" -> { nativeVideoDone(); "0" }
                "og_quickslots" -> nativeQuickSlots()
                "og_potslots" -> nativePotSlots()
                "og_quickpanel" -> { nativeQuickPanel(a.getInt(0), a.getInt(1)); "0" }
                "og_pointer" -> { nativePointer(a.getInt(0), a.getInt(1), a.getInt(2)); "0" }
                "og_input_look" -> { nativeLook(a.getInt(0), a.getInt(1)); "0" }
                "og_input_zoom" -> { nativeZoom(a.getInt(0)); "0" }
                else -> "-1"
            }
        }

        // the renderer (web/shell.html #renderer-sel): "webgl2" = OpenGL ES, else Vulkan; the game restarts with it
        @JavascriptInterface
        fun setRenderer(v: String) = runOnUiThread { switchRenderer(v == "webgl2") }

        // the layout of the touch controls from the app's Controls tab (written by MainActivity)
        @JavascriptInterface
        fun controls(): String = try { File(filesDir, "controls.json").readText() } catch (e: Exception) { "{}" }

        @JavascriptInterface
        fun setControls(json: String) {
            try { org.json.JSONObject(json); File(filesDir, "controls.json").writeText(json) } catch (e: Exception) { Log.e(TAG, "controls: ${e.message}") }
        }

        // the phone's processor: "SoC name|cores|max MHz"
        @JavascriptInterface
        fun cpuInfo(): String = cpuDescription()

        // "Exit": to the app's main menu. The game runs in its own process (":game", it cannot be started twice),
        // which ends with the screen; the main menu is the activity under it
        @JavascriptInterface
        fun exitGame() { runOnUiThread { leaveGame() } }

        // the sky setting: the textures of the other game come from the server once; the page asks for the download and
        // watches `skyStatus` ("idle" / "loading <percent>" / "ready" / "error <text>")
        @JavascriptInterface
        fun skyEnsure(sky: Int) = SkyPack.ensure(applicationContext, sky)

        @JavascriptInterface
        fun skyStatus(): String = SkyPack.status

        @JavascriptInterface
        fun key(action: Int, code: String) = nativeWebKey(action, code)
    }

    @SuppressLint("SetJavaScriptEnabled", "AddJavascriptInterface")
    private fun createUiView(): View {
        val wv = WebView(this)
        wv.setBackgroundColor(Color.TRANSPARENT)
        wv.settings.javaScriptEnabled = true
        wv.settings.domStorageEnabled = true
        // cutscenes: the engine writes an mp4 to the game folder, the page plays it from there
        wv.settings.allowFileAccess = true
        @Suppress("DEPRECATION") run {
            wv.settings.allowFileAccessFromFileURLs = true
            wv.settings.allowUniversalAccessFromFileURLs = true
        }
        wv.settings.mediaPlaybackRequiresUserGesture = false
        wv.overScrollMode = View.OVER_SCROLL_NEVER
        wv.webChromeClient = object : WebChromeClient() {
            // the white play icon the WebView shows in a <video> until the first frame
            override fun getDefaultVideoPoster(): android.graphics.Bitmap? =
                android.graphics.Bitmap.createBitmap(1, 1, android.graphics.Bitmap.Config.ARGB_8888).also { it.eraseColor(Color.BLACK) }

            override fun onConsoleMessage(m: android.webkit.ConsoleMessage): Boolean {
                Log.i(TAG, "ui: ${m.message()} (${m.sourceId()}:${m.lineNumber()})")
                return true
            }
        }
        wv.webViewClient = WebViewClient()
        wv.addJavascriptInterface(UiBridge(), "AndroidBridge")
        wv.loadUrl("file:///android_asset/ui.html")
        webView = wv
        return wv
    }

    override fun onResume() {
        super.onResume()
        // back from the background with the process alive: nothing to restore
        try { File(gameDir, RESUME_MARK).delete() } catch (e: Exception) {}
        hideSystemBars()
    }

    // the app leaves the screen (home, the screen is off, another app): Android may end the process of the game there,
    // so the game is saved first (the hidden slot 99, "Autosave") and a marker is left in the game folder. If the process
    // is killed, the next start of the game (also the one Android makes when the player comes back) loads the slot
    override fun onStop() {
        try {
            if (!switching && !isFinishing && nativeIsGameRunning() && nativeAutoSave() == 1) {
                // the game thread does it (it needs no window: here the surface is already gone): 1 asked, 2 runs, 3 written, 4 not possible
                val end = System.currentTimeMillis() + 7000
                var st = nativeSaveBusy()
                while ((st == 1 || st == 2) && System.currentTimeMillis() < end) { Thread.sleep(50); st = nativeSaveBusy() }
                Log.i(TAG, "onStop: autosave " + when (st) { 3 -> "written"; 4 -> "not possible now"; else -> "not finished" })
                // the marker only after a finished save: a save that was cut off is not loaded
                if (st == 3) File(gameDir, RESUME_MARK).writeText("1")
            }
        } catch (e: Throwable) {
            Log.w(TAG, "autosave: ${e.message}")
        }
        super.onStop()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemBars()
        }
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    private var exitWatch: Thread? = null
    // the renderer is chosen at start: the game is saved (the hidden slot 99), the app's main menu starts it again in a new
    // process (this one ends) and the new start loads the slot - the player comes back where he was
    private fun switchRenderer(gl: Boolean) {
        if (RendererPref.isGl(this) == gl)
            return
        RendererPref.set(this, gl)
        switching = true
        Thread {
            var saved = false
            try {
                if (nativeIsGameRunning() && nativeAutoSave() == 1) {
                    val end = System.currentTimeMillis() + 20000
                    var st = nativeSaveBusy()
                    while ((st == 1 || st == 2) && System.currentTimeMillis() < end) { Thread.sleep(50); st = nativeSaveBusy() }
                    saved = (st == 3)
                }
            } catch (e: Throwable) {
                Log.w(TAG, "renderer switch: ${e.message}")
            }
            Log.i(TAG, "renderer switch to " + (if (gl) "OpenGL ES" else "Vulkan") + ", saved=$saved")
            runOnUiThread {
                try { if (saved) File(gameDir, RESUME_MARK).writeText("1") } catch (e: Exception) {}
                val back = Intent(this, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
                    putExtra(MainActivity.EXTRA_RELAUNCH, intent.extras ?: Bundle())
                }
                startActivity(back)
                finish()
                Handler(Looper.getMainLooper()).postDelayed({ android.os.Process.killProcess(android.os.Process.myPid()) }, 400)
            }
        }.start()
    }

    private fun leaveGame() {
        try { File(gameDir, RESUME_MARK).delete() } catch (e: Exception) {}
        finish()
        Handler(Looper.getMainLooper()).postDelayed({ android.os.Process.killProcess(android.os.Process.myPid()) }, 400)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.i(TAG, "GameActivity surfaceCreated: valid=${holder.surface.isValid}")
        surfaceHolder = holder
        nativeSetSurface(holder.surface, gameDir)
        // the game's own menu "Exit" ends the engine loop: go back to the app's main menu
        if (exitWatch == null) exitWatch = Thread {
            while (true) {
                try { Thread.sleep(250) } catch (e: InterruptedException) { return@Thread }
                if (nativeExitRequested()) { Log.i(TAG, "game requested exit"); runOnUiThread { leaveGame() }; return@Thread }
            }
        }.also { it.isDaemon = true; it.start() }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.i(TAG, "GameActivity surfaceChanged: ${width}x${height}, format=$format")
        nativeSetDensity(resources.displayMetrics.density)
        nativeSurfaceChanged(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.i(TAG, "GameActivity surfaceDestroyed")
        nativeDestroySurface()
        surfaceHolder = null
    }

    // back opens the side menu of the page (it is closed by back again); the game's own menu is an item in it
    private fun backPressed() {
        webView?.evaluateJavascript("window.ogBackMenu && window.ogBackMenu()", null)
    }

    private fun cpuDescription(): String {
        var name = ""
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            name = listOf(Build.SOC_MANUFACTURER, Build.SOC_MODEL).filter { it.isNotBlank() && it != Build.UNKNOWN }.joinToString(" ")
        }
        if (name.isBlank()) {
            try {
                File("/proc/cpuinfo").forEachLine { l ->
                    if (name.isBlank() && l.startsWith("Hardware")) name = l.substringAfter(':').trim()
                }
            } catch (e: Exception) {}
        }
        if (name.isBlank()) name = Build.HARDWARE
        var maxKhz = 0L
        val cores = Runtime.getRuntime().availableProcessors()
        for (i in 0 until cores) {
            try {
                maxKhz = maxOf(maxKhz, File("/sys/devices/system/cpu/cpu$i/cpufreq/cpuinfo_max_freq").readText().trim().toLong())
            } catch (e: Exception) {}
        }
        return "$name|$cores|${maxKhz / 1000}"
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyDown(keyCode, event)
        nativeOnKey(0, keyCode)
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyUp(keyCode, event)
        nativeOnKey(1, keyCode)
        return super.onKeyUp(keyCode, event)
    }
}
