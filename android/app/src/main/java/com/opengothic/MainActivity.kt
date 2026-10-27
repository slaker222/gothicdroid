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

    // an installed game: a folder with Data/*.vdf
    private data class GameEntry(val path: String, val title: String)

    private fun hasVdf(dir: File): Boolean {
        val data = File(dir, "Data")
        return data.isDirectory && (data.listFiles { _, name -> name.endsWith(".vdf", ignoreCase = true) }?.isNotEmpty() == true)
    }

    // the games found next to each other on the storage, the first of each kind
    private fun findGames(): List<GameEntry> {
        val roots = listOf("/storage/emulated/0", "/sdcard")
        val known = listOf(
            "Gothic2"     to "Gothic II: Ночь Ворона",
            "GothicSmall" to "Gothic (малая версия)",
            "Gothic1"     to "Gothic",
            "Gothic"      to "Gothic"
        )
        val out = ArrayList<GameEntry>()
        val seen = HashSet<String>()
        for ((name, title) in known) {
            for (root in roots) {
                val dir = File(root, name)
                if (!hasVdf(dir)) continue
                val real = try { dir.canonicalPath } catch (e: Exception) { dir.path }
                if (seen.add(real)) {
                    out.add(GameEntry(dir.path, title))
                    break
                }
            }
        }
        val own = getExternalFilesDir(null)?.absolutePath?.let { File(it, "Gothic2") }
        if (own != null && hasVdf(own) && seen.add(own.path))
            out.add(GameEntry(own.path, "Gothic II (папка приложения)"))
        return out
    }

    private fun findGameDirectory(): String {
        val games = findGames()
        val saved = getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE).getString("game_dir", null)
        if (saved != null && games.any { it.path == saved }) {
            Log.i(TAG, "Selected game directory: $saved")
            return saved
        }
        if (games.isNotEmpty()) {
            Log.i(TAG, "Found game directory: ${games[0].path}")
            return games[0].path
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

    private fun updateDebugCfg(scalePercent: Int, simpleRender: Boolean) {
        try {
            val dir = File(detectedGameDir)
            if (!dir.exists()) dir.mkdirs()
            val prefs = getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE)
            val values = linkedMapOf(
                "render_scale" to scalePercent.toString(),
                "simple_render" to (if (simpleRender) "1" else "0"),
                "draw_dist" to prefs.getInt("draw_dist", 0).toString(),
                "world_dist" to prefs.getInt("world_dist", 0).toString()
            )
            val cfgFile = File(dir, "debug.cfg")
            val lines = if (cfgFile.exists()) cfgFile.readLines().toMutableList() else mutableListOf()
            val done = HashSet<String>()
            for (i in lines.indices) {
                val key = lines[i].substringBefore('=', "")
                val v = values[key] ?: continue
                lines[i] = "$key=$v"
                done.add(key)
            }
            for ((k, v) in values) if (k !in done) lines.add("$k=$v")
            cfgFile.writeText(lines.joinToString("\n") + "\n")
        } catch (e: Exception) {
            Log.e(TAG, "updateDebugCfg error: ${e.message}")
        }
    }

    private fun launchGame() {
        val prefs = getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE)
        val p = prefs.getInt("render_scale", 100)
        val s = prefs.getBoolean("simple_render", false)
        updateDebugCfg(p, s)

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

        // --- GAME CHOICE ---
        val games = findGames()
        if (games.size > 1) {
            val gameCard = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                background = GradientDrawable().apply {
                    setColor(Color.parseColor("#1C2025"))
                    cornerRadius = dp(10).toFloat()
                    setStroke(dp(1), Color.parseColor("#2E353D"))
                }
                setPadding(dp(16), dp(12), dp(16), dp(12))
            }
            gameCard.addView(TextView(this).apply {
                textSize = 14f
                setTypeface(Typeface.DEFAULT_BOLD)
                setTextColor(Color.parseColor("#E0C068"))
                text = "Игра"
                setPadding(0, 0, 0, dp(4))
            })
            val group = RadioGroup(this).apply { orientation = RadioGroup.VERTICAL }
            val ids = HashMap<Int, String>()
            for (g in games) {
                val rb = RadioButton(this).apply {
                    id = View.generateViewId()
                    text = g.title + "\n" + g.path
                    textSize = 13f
                    setTextColor(Color.WHITE)
                    isChecked = (g.path == detectedGameDir)
                }
                ids[rb.id] = g.path
                group.addView(rb)
            }
            group.setOnCheckedChangeListener { _, checkedId ->
                val path = ids[checkedId] ?: return@setOnCheckedChangeListener
                getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE).edit().putString("game_dir", path).apply()
                detectedGameDir = path
                val pr = getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE)
                updateDebugCfg(pr.getInt("render_scale", 100), pr.getBoolean("simple_render", false))
                updateStatus()
                updateLogView()
            }
            gameCard.addView(group)
            mainLayout.addView(gameCard)
            mainLayout.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(14)) })
        }

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
        mainLayout.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(14)) })

        // --- RESOLUTION SCALE SLIDER CARD ---
        val scaleCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#1C2025"))
                cornerRadius = dp(10).toFloat()
                setStroke(dp(1), Color.parseColor("#2E353D"))
            }
            setPadding(dp(16), dp(12), dp(16), dp(12))
        }

        val prefs = getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE)
        var currentScalePercent = prefs.getInt("render_scale", 100)

        val scaleLabel = TextView(this).apply {
            textSize = 14f
            setTypeface(Typeface.DEFAULT_BOLD)
            setTextColor(Color.parseColor("#E0C068"))
            text = "Разрешение рендеринга: $currentScalePercent%"
        }
        scaleCard.addView(scaleLabel)

        val scaleDesc = TextView(this).apply {
            textSize = 11f
            setTextColor(Color.parseColor("#9AA0A6"))
            text = "100% (Native 2400x1080) • 75% • 50% • 40% • 30% • 25% • 20% (Макс. FPS)"
            setPadding(0, dp(2), 0, dp(6))
        }
        scaleCard.addView(scaleDesc)

        // discrete steps, left to right
        val scaleValues = intArrayOf(20, 25, 30, 40, 50, 75, 100)
        var initialStep = scaleValues.indexOf(currentScalePercent)
        if (initialStep < 0) initialStep = scaleValues.size - 1

        val scaleSeekBar = SeekBar(this).apply {
            max = scaleValues.size - 1
            progress = initialStep
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    val p = scaleValues[progress.coerceIn(0, scaleValues.size - 1)]
                    currentScalePercent = p
                    scaleLabel.text = "Разрешение рендеринга: $p%"
                    prefs.edit().putInt("render_scale", p).apply()
                    val s = prefs.getBoolean("simple_render", false)
                    updateDebugCfg(p, s)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }
        scaleCard.addView(scaleSeekBar)
        mainLayout.addView(scaleCard)

        mainLayout.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(12)) })

        // --- DISTANCE LIMITS ---
        val distCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#1C2025"))
                cornerRadius = dp(10).toFloat()
                setStroke(dp(1), Color.parseColor("#2E353D"))
            }
            setPadding(dp(16), dp(12), dp(16), dp(12))
        }
        val distNames = arrayOf("без ограничения", "150 м", "80 м", "40 м", "20 м")
        val worldValues = intArrayOf(0, 400, 250, 150, 100)
        val worldNames = arrayOf("без ограничения", "400 м", "250 м", "150 м", "100 м")

        fun addSteps(title: String, key: String, values: IntArray, names: Array<String>, hint: String) {
            val cur = prefs.getInt(key, 0)
            val label = TextView(this).apply {
                textSize = 14f
                setTypeface(Typeface.DEFAULT_BOLD)
                setTextColor(Color.parseColor("#E0C068"))
            }
            val idx0 = values.indexOf(cur).let { if (it < 0) 0 else it }
            label.text = "$title: ${names[idx0]}"
            distCard.addView(label)
            distCard.addView(TextView(this).apply {
                textSize = 11f
                setTextColor(Color.parseColor("#9AA0A6"))
                text = hint
                setPadding(0, dp(2), 0, dp(4))
            })
            distCard.addView(SeekBar(this).apply {
                max = values.size - 1
                progress = idx0
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                        label.text = "$title: ${names[progress]}"
                        prefs.edit().putInt(key, values[progress]).apply()
                        updateDebugCfg(prefs.getInt("render_scale", 100), prefs.getBoolean("simple_render", false))
                    }
                    override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                    override fun onStopTrackingTouch(seekBar: SeekBar?) {}
                })
            })
        }
        addSteps("Дальность объектов", "draw_dist", intArrayOf(0, 1, 2, 3, 4), distNames,
            "Объекты и NPC дальше не рисуются и не анимируются (больше FPS)")
        distCard.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(8)) })
        addSteps("Дальность мира", "world_dist", worldValues, worldNames,
            "Всё, включая ландшафт, дальше этого не рисуется")
        mainLayout.addView(distCard)

        // Space
        mainLayout.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(12)) })

        // --- RENDER MODE SELECTOR ---
        val modeCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#1C2025"))
                cornerRadius = dp(10).toFloat()
                setStroke(dp(1), Color.parseColor("#2E353D"))
            }
            setPadding(dp(16), dp(12), dp(16), dp(12))
        }

        var currentSimpleRender = prefs.getBoolean("simple_render", false)
        val modeLabel = TextView(this).apply {
            textSize = 14f
            setTypeface(Typeface.DEFAULT_BOLD)
            setTextColor(Color.parseColor("#E0C068"))
            text = "Режим графики: " + (if (currentSimpleRender) "Простой (GothicEngine)" else "Стандартный (OpenGothic)")
        }
        modeCard.addView(modeLabel)

        val modeDesc = TextView(this).apply {
            textSize = 11f
            setTextColor(Color.parseColor("#9AA0A6"))
            text = "Простой рендер: прямое освещение, отключение тяжелых SSAO/GWater/PointLights (макс. FPS)"
            setPadding(0, dp(2), 0, dp(8))
        }
        modeCard.addView(modeDesc)

        val modeSwitch = android.widget.Switch(this).apply {
            text = "Включить простой рендер (GothicEngine)"
            setTextColor(Color.parseColor("#D0D5DD"))
            textSize = 13f
            isChecked = currentSimpleRender
            setOnCheckedChangeListener { _, isChecked ->
                currentSimpleRender = isChecked
                modeLabel.text = "Режим графики: " + (if (isChecked) "Простой (GothicEngine)" else "Стандартный (OpenGothic)")
                prefs.edit().putBoolean("simple_render", isChecked).apply()
                val p = prefs.getInt("render_scale", 100)
                updateDebugCfg(p, isChecked)
            }
        }
        modeCard.addView(modeSwitch)
        mainLayout.addView(modeCard)

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
