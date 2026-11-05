package com.opengothic

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
        const val EXTRA_MOD = "extra_mod"   // the ini of a mod in the game folder (Archolos), "" - none

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
    private external fun nativeSetMod(mod: String)
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
    private external fun nativeAutoSave(): Int
    private external fun nativeSaveBusy(): Int
    private external fun nativeQuests(): String
    private external fun nativeVideoTake(): String
    private external fun nativeVideoDone()
    private external fun nativeQuickSlots(): String
    private external fun nativeQuickPanel(open: Int, first: Int)
    private external fun nativePointer(type: Int, x: Int, y: Int)
    private external fun nativeLook(dx: Int, dy: Int)
    private external fun nativeWebKey(action: Int, code: String)

    private lateinit var surfaceView: SurfaceView
    private var webView: WebView? = null
    private var surfaceHolder: SurfaceHolder? = null
    private var gameDir: String = "/sdcard/Gothic2"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "GameActivity onCreate")

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        hideSystemBars()

        gameDir = intent.getStringExtra(EXTRA_GAME_DIR) ?: "/sdcard/Gothic2"
        nativeSetMod(intent.getStringExtra(EXTRA_MOD) ?: "")

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
                "og_quickpanel" -> { nativeQuickPanel(a.getInt(0), a.getInt(1)); "0" }
                "og_pointer" -> { nativePointer(a.getInt(0), a.getInt(1), a.getInt(2)); "0" }
                "og_input_look" -> { nativeLook(a.getInt(0), a.getInt(1)); "0" }
                else -> "-1"
            }
        }

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
        hideSystemBars()
    }

    // the app leaves the screen (home, the screen is off, another app): Android may end the process of the game there,
    // so the game is saved first (the quick save slot, named "Autosave"). The surface lives until onStop returns,
    // the engine keeps drawing meanwhile: wait for the end of the save, but not longer than a few seconds
    override fun onStop() {
        try {
            val started = !isFinishing && nativeIsGameRunning() && nativeAutoSave() == 1
            Log.i(TAG, "onStop: autosave ${if (started) "started" else "not possible now"}")
            if (started) {
                val end = System.currentTimeMillis() + 7000
                while (nativeSaveBusy() == 1 && System.currentTimeMillis() < end) Thread.sleep(50)
                Log.i(TAG, "autosave: ${if (nativeSaveBusy() == 1) "still running" else "done"}")
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
    private fun leaveGame() {
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
