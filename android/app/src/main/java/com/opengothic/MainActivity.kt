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
import android.view.Gravity
import android.view.View
import android.widget.*
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

    private lateinit var statusTextView: TextView
    private lateinit var logConsoleTextView: TextView
    private var detectedGameDir: String = "/sdcard/Gothic2"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "MainActivity (Launcher) onCreate")

        checkAndRequestStoragePermissions()
        detectedGameDir = findGameDirectory()

        setContentView(buildLauncherLayout())
        updateStatus()
        updateLogView()
    }

    override fun onResume() {
        super.onResume()
        updateStatus()
        updateLogView()
    }

    private fun checkAndRequestStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Log.i(TAG, "Requesting MANAGE_APP_ALL_FILES_ACCESS_PERMISSION")
                Toast.makeText(
                    this,
                    "Для чтения файлов игры Gothic II требуется доступ ко всем файлам",
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

    private fun findGameDirectory(): String {
        val candidates = listOf(
            "/storage/emulated/0/Gothic2",
            "/sdcard/Gothic2",
            "/storage/emulated/0/Gothic",
            "/sdcard/Gothic",
            getExternalFilesDir(null)?.absolutePath + "/Gothic2"
        )
        for (path in candidates) {
            val dir = File(path)
            if (dir.exists() && dir.isDirectory) {
                Log.i(TAG, "Found game directory: $path")
                return path
            }
        }
        return "/sdcard/Gothic2"
    }

    private fun updateStatus() {
        detectedGameDir = findGameDirectory()
        val dir = File(detectedGameDir)
        val dataDir = File(dir, "Data")
        val vdfFiles = if (dataDir.exists()) dataDir.listFiles { _, name -> name.endsWith(".vdf", ignoreCase = true) } else null

        if (dir.exists() && vdfFiles != null && vdfFiles.isNotEmpty()) {
            statusTextView.setTextColor(Color.parseColor("#4CAF50"))
            statusTextView.text = "✓ Путь: $detectedGameDir\n✓ Ресурсы найдены: ${vdfFiles.size} VDF архивов в Data/"
        } else if (dir.exists()) {
            statusTextView.setTextColor(Color.parseColor("#FFC107"))
            statusTextView.text = "⚠ Путь: $detectedGameDir найден, но папка Data/ не содержит .vdf архивов"
        } else {
            statusTextView.setTextColor(Color.parseColor("#FF5252"))
            statusTextView.text = "✗ Папка не найдена: $detectedGameDir\nСкопируйте Gothic II в $detectedGameDir"
        }
    }

    private fun readCompleteLog(): String {
        val sb = StringBuilder()

        // 1. Check crash.log in game directory
        val crashFile = File(detectedGameDir, "crash.log")
        if (crashFile.exists()) {
            sb.append("=== CRASH LOG (/sdcard/Gothic2/crash.log) ===\n")
            sb.append(crashFile.readText())
            sb.append("\n\n")
        }

        // 2. Check Java crash log
        val javaCrashFile = File(filesDir, "last_crash.txt")
        if (javaCrashFile.exists()) {
            sb.append("=== JAVA CRASH LOG ===\n")
            sb.append(javaCrashFile.readText())
            sb.append("\n\n")
        }

        // 3. Read native in-memory log
        try {
            val nativeLog = nativeGetLastLog()
            if (nativeLog.isNotBlank()) {
                sb.append("=== NATIVE LOG ===\n")
                sb.append(nativeLog)
                sb.append("\n\n")
            }
        } catch (e: Exception) {
            sb.append("nativeGetLastLog error: ${e.message}\n")
        }

        // 4. Check opengothic_log.txt file
        val sessionLogFile = File(detectedGameDir, "opengothic_log.txt")
        if (sessionLogFile.exists() && sb.length < 500) {
            sb.append("=== LOG FILE ===\n")
            sb.append(sessionLogFile.readText())
        }

        return if (sb.isNotBlank()) sb.toString() else "Лог пуст. Запустите игру для сбора логов."
    }

    private fun updateLogView() {
        val logText = readCompleteLog()
        logConsoleTextView.text = logText
    }

    private fun copyLogToClipboard() {
        val logText = readCompleteLog()
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("OpenGothic Log", logText)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, "Лог скопирован в буфер обмена! (${logText.length} симв.)", Toast.LENGTH_SHORT).show()
    }

    private fun launchGame() {
        val intent = Intent(this, GameActivity::class.java).apply {
            putExtra(GameActivity.EXTRA_GAME_DIR, detectedGameDir)
        }
        startActivity(intent)
    }

    private fun buildLauncherLayout(): View {
        val dp = { v: Int -> (v * resources.displayMetrics.density).toInt() }

        val rootScrollView = ScrollView(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.parseColor("#121417"))
            isFillViewport = true
        }

        val mainLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )
            setPadding(dp(20), dp(24), dp(20), dp(24))
        }

        val appVersion = try {
            packageManager.getPackageInfo(packageName, 0).versionName ?: "0.01"
        } catch (e: Exception) {
            "0.01"
        }

        // --- TITLE ---
        val titleText = TextView(this).apply {
            text = "OPENGOTHIC ($appVersion)"
            textSize = 28f
            setTypeface(Typeface.DEFAULT_BOLD)
            setTextColor(Color.parseColor("#E0C068")) // Gothic Gold
            gravity = Gravity.CENTER_HORIZONTAL
        }
        mainLayout.addView(titleText)

        val subtitleText = TextView(this).apply {
            text = "Android Edition • Ночь Ворона"
            textSize = 14f
            setTextColor(Color.parseColor("#9AA0A6"))
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(0, 0, 0, dp(16))
        }
        mainLayout.addView(subtitleText)

        // --- STATUS CARD ---
        val statusCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#1C2025"))
                cornerRadius = dp(10).toFloat()
                setStroke(dp(1), Color.parseColor("#2E353D"))
            }
            setPadding(dp(16), dp(14), dp(16), dp(14))
        }

        statusTextView = TextView(this).apply {
            textSize = 13f
            typeface = Typeface.MONOSPACE
            setTextColor(Color.WHITE)
            text = "Проверка директории игры..."
        }
        statusCard.addView(statusTextView)
        mainLayout.addView(statusCard)

        // Space
        mainLayout.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(16)) })

        // --- RUN BUTTON ---
        val runButton = Button(this).apply {
            text = "▶  ЗАПУСТИТЬ ИГРУ"
            textSize = 17f
            setTypeface(Typeface.DEFAULT_BOLD)
            setTextColor(Color.parseColor("#121417"))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#E0C068")) // Gold button
                cornerRadius = dp(8).toFloat()
            }
            setPadding(dp(16), dp(14), dp(16), dp(14))
            setOnClickListener { launchGame() }
        }
        mainLayout.addView(runButton)

        // Space
        mainLayout.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(12)) })

        // --- BUTTONS ROW (COPY LOG & REFRESH) ---
        val buttonsRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )
        }

        val copyLogButton = Button(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
                marginEnd = dp(6)
            }
            text = "📋 СКОПИРОВАТЬ ЛОГ"
            textSize = 13f
            setTextColor(Color.WHITE)
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#2A313A"))
                cornerRadius = dp(8).toFloat()
                setStroke(dp(1), Color.parseColor("#444E5B"))
            }
            setOnClickListener { copyLogToClipboard() }
        }
        buttonsRow.addView(copyLogButton)

        val refreshButton = Button(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
                marginStart = dp(6)
            }
            text = "🔄 ОБНОВИТЬ"
            textSize = 13f
            setTextColor(Color.WHITE)
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#2A313A"))
                cornerRadius = dp(8).toFloat()
                setStroke(dp(1), Color.parseColor("#444E5B"))
            }
            setOnClickListener {
                updateStatus()
                updateLogView()
                Toast.makeText(this@MainActivity, "Статус и лог обновлены", Toast.LENGTH_SHORT).show()
            }
        }
        buttonsRow.addView(refreshButton)
        mainLayout.addView(buttonsRow)

        // Space
        mainLayout.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(18)) })

        // --- LOG CONSOLE HEADER ---
        val logHeader = TextView(this).apply {
            text = "Лог игры / Отчёт об ошибках:"
            textSize = 14f
            setTypeface(Typeface.DEFAULT_BOLD)
            setTextColor(Color.parseColor("#C4C7C5"))
            setPadding(0, 0, 0, dp(6))
        }
        mainLayout.addView(logHeader)

        // --- LOG CONSOLE BOX ---
        val logCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#0A0C0E"))
                cornerRadius = dp(8).toFloat()
                setStroke(dp(1), Color.parseColor("#1F242A"))
            }
            setPadding(dp(12), dp(10), dp(12), dp(10))
        }

        logConsoleTextView = TextView(this).apply {
            textSize = 11f
            typeface = Typeface.MONOSPACE
            setTextColor(Color.parseColor("#80E090")) // Green console text
            setTextIsSelectable(true)
            text = "Инициализация..."
        }
        logCard.addView(logConsoleTextView)
        mainLayout.addView(logCard)

        rootScrollView.addView(mainLayout)
        return rootScrollView
    }
}
