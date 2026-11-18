package com.opengothic

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class CrashActivity : Activity() {

    companion object {
        const val EXTRA_GAME_DIR = "game_dir"
        const val EXTRA_CRASH_SOURCE = "crash_source"
        const val EXTRA_CRASH_DETAILS = "crash_details"
    }

    private var gameDir: String = "/sdcard/Gothic2"
    private var crashSource: String = "Crash"
    private var crashDetails: String? = null

    private lateinit var tvTitle: TextView
    private lateinit var tvSubtitle: TextView
    private lateinit var tvLog: TextView
    private lateinit var svLog: ScrollView
    private lateinit var spLogs: Spinner

    private data class LogEntry(val label: String, val file: File, val isCrash: Boolean)

    private val logEntries = mutableListOf<LogEntry>()
    private var currentLogText: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        gameDir = intent.getStringExtra(EXTRA_GAME_DIR) ?: "/sdcard/Gothic2"
        crashSource = intent.getStringExtra(EXTRA_CRASH_SOURCE) ?: "Unknown Crash"
        crashDetails = intent.getStringExtra(EXTRA_CRASH_DETAILS)

        setContentView(buildUi())
        loadLogList()
    }

    private fun dp(v: Int): Int {
        return TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics
        ).toInt()
    }

    private fun createBtn(text: String, bgNormal: Int, bgStroke: Int, onClick: () -> Unit): Button {
        val btn = Button(this)
        btn.text = text
        btn.isAllCaps = false
        btn.setTextColor(Color.WHITE)
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f)
        btn.setTypeface(Typeface.DEFAULT_BOLD)
        btn.setPadding(dp(14), dp(6), dp(14), dp(6))

        val bg = GradientDrawable().apply {
            setColor(bgNormal)
            setStroke(dp(1), bgStroke)
            cornerRadius = dp(6).toFloat()
        }
        btn.background = bg
        btn.setOnClickListener { onClick() }
        return btn
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.parseColor("#141619"))
            setPadding(dp(12), dp(10), dp(12), dp(10))
        }

        // --- 1. Header Row ---
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }

        val badge = TextView(this).apply {
            text = " ОШИБКА / CRASH "
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.DEFAULT_BOLD
            setPadding(dp(8), dp(3), dp(8), dp(3))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#9e2a2b"))
                cornerRadius = dp(4).toFloat()
            }
        }
        header.addView(badge)

        tvTitle = TextView(this).apply {
            text = " Игра неожиданно завершила работу"
            setTextColor(Color.parseColor("#f0f3f8"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            typeface = Typeface.DEFAULT_BOLD
            setPadding(dp(8), 0, 0, 0)
        }
        header.addView(tvTitle)
        root.addView(header)

        // Subtitle with crash context
        val timeStr = SimpleDateFormat("dd.MM.yyyy HH:mm:ss", Locale.getDefault()).format(Date())
        tvSubtitle = TextView(this).apply {
            text = "Причина: $crashSource  |  Время: $timeStr  |  Папка: $gameDir"
            setTextColor(Color.parseColor("#8c929d"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f)
            setPadding(dp(2), dp(3), 0, dp(6))
        }
        root.addView(tvSubtitle)

        // --- 2. Action Buttons (HorizontalScrollView) ---
        val btnScroll = HorizontalScrollView(this).apply {
            isHorizontalScrollBarEnabled = false
            setPadding(0, dp(2), 0, dp(6))
        }
        val btnRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
        }

        val btnParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dp(38)
        ).apply {
            setMargins(0, 0, dp(8), 0)
        }

        val btnCopy500 = createBtn("✂ Скопировать последние 500 строк", Color.parseColor("#254b77"), Color.parseColor("#3a74b4")) {
            copyLast500Lines()
        }
        btnRow.addView(btnCopy500, btnParams)

        val btnCopyAll = createBtn("📋 Скопировать весь лог", Color.parseColor("#2a2d34"), Color.parseColor("#3f4450")) {
            copyAllLog()
        }
        btnRow.addView(btnCopyAll, btnParams)

        val btnShare = createBtn("📤 Поделиться", Color.parseColor("#2a2d34"), Color.parseColor("#3f4450")) {
            shareLog()
        }
        btnRow.addView(btnShare, btnParams)

        val btnRestart = createBtn("🔄 Перезапустить игру", Color.parseColor("#2b593f"), Color.parseColor("#3f875f")) {
            restartGame()
        }
        btnRow.addView(btnRestart, btnParams)

        val btnClose = createBtn("🏠 В меню", Color.parseColor("#2a2d34"), Color.parseColor("#3f4450")) {
            closeToLauncher()
        }
        btnRow.addView(btnClose, btnParams)

        btnScroll.addView(btnRow)
        root.addView(btnScroll)

        // --- 3. Log Selector Row ---
        val selectorRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(2), dp(2), 0, dp(6))
        }

        val lblSelector = TextView(this).apply {
            text = "Просмотр лога: "
            setTextColor(Color.parseColor("#b0b5be"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            typeface = Typeface.DEFAULT_BOLD
        }
        selectorRow.addView(lblSelector)

        spLogs = Spinner(this).apply {
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#20232a"))
                setStroke(dp(1), Color.parseColor("#3a3f4b"))
                cornerRadius = dp(4).toFloat()
            }
            setPadding(dp(8), dp(4), dp(8), dp(4))
        }
        selectorRow.addView(spLogs, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(34)))
        root.addView(selectorRow)

        // --- 4. Log Content Area ---
        svLog = ScrollView(this).apply {
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#0a0b0d"))
                setStroke(dp(1), Color.parseColor("#23262d"))
                cornerRadius = dp(6).toFloat()
            }
            setPadding(dp(10), dp(10), dp(10), dp(10))
        }

        tvLog = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(Color.parseColor("#d4d8e0"))
            setTextIsSelectable(true)
            setHorizontallyScrolling(false)
        }
        svLog.addView(tvLog)

        val logParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0
        ).apply {
            weight = 1.0f
        }
        root.addView(svLog, logParams)

        return root
    }

    private fun loadLogList() {
        logEntries.clear()
        val dir = File(gameDir)

        // 1. Current crash.log
        val crashLog = File(dir, "crash.log")
        if (crashLog.exists() && crashLog.length() > 0) {
            logEntries.add(LogEntry("Текущий вылет (crash.log)", crashLog, true))
        }

        // 2. Current session log opengothic_log.txt
        val sessionLog = File(dir, "opengothic_log.txt")
        if (sessionLog.exists() && sessionLog.length() > 0) {
            logEntries.add(LogEntry("Текущая сессия (opengothic_log.txt)", sessionLog, false))
        }

        // 3. Historical crash logs (crash_1.log .. crash_5.log)
        for (i in 1..LogManager.MAX_LOGS) {
            val f = File(dir, "crash_$i.log")
            if (f.exists() && f.length() > 0) {
                logEntries.add(LogEntry("Предыдущий вылет #$i (crash_$i.log)", f, true))
            }
        }

        // 4. Historical session logs (opengothic_log_1.txt .. opengothic_log_5.txt)
        for (i in 1..LogManager.MAX_LOGS) {
            val f = File(dir, "opengothic_log_$i.txt")
            if (f.exists() && f.length() > 0) {
                logEntries.add(LogEntry("Предыдущая сессия #$i (opengothic_log_$i.txt)", f, false))
            }
        }

        // 5. Java crash log in filesDir
        val javaCrash = File(filesDir, "last_crash.txt")
        if (javaCrash.exists() && javaCrash.length() > 0) {
            logEntries.add(LogEntry("Java crash log (last_crash.txt)", javaCrash, true))
        }

        if (logEntries.isEmpty()) {
            val dummyText = if (!crashDetails.isNullOrBlank()) {
                "=== ДЕТАЛИ ОШИБКИ ===\n$crashDetails"
            } else {
                "Лог-файлы в папке $gameDir не найдены."
            }
            tvLog.text = dummyText
            currentLogText = dummyText
            return
        }

        val labels = logEntries.map { it.label }
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)
        spLogs.adapter = adapter

        spLogs.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (position in logEntries.indices) {
                    displayLogFile(logEntries[position].file)
                }
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        // Default to first item
        displayLogFile(logEntries[0].file)
    }

    private fun displayLogFile(file: File) {
        if (!file.exists()) {
            tvLog.text = "Файл ${file.name} не существует."
            currentLogText = ""
            return
        }

        try {
            val text = file.readText()
            currentLogText = text

            // If file is very large (> 400KB), truncate display for TextView performance,
            // but currentLogText keeps full text for Copy/Share.
            if (text.length > 400000) {
                val banner = "...\n[Файл большой (${file.length() / 1024} КБ). Показаны последние 400 000 символов.\nКнопки копирования скопируют полный лог.]\n\n"
                tvLog.text = banner + text.substring(text.length - 400000)
            } else {
                tvLog.text = text
            }

            svLog.post { svLog.fullScroll(View.FOCUS_DOWN) }
        } catch (e: Exception) {
            tvLog.text = "Ошибка чтения ${file.name}: ${e.message}"
            currentLogText = ""
        }
    }

    private fun copyLast500Lines() {
        if (currentLogText.isBlank()) {
            Toast.makeText(this, "Лог пуст", Toast.LENGTH_SHORT).show()
            return
        }
        val lines = currentLogText.lines()
        val count = minOf(500, lines.size)
        val tail = lines.takeLast(count).joinToString("\n")

        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("OpenGothic Log Tail", tail)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, "Скопировано последних $count строк в буфер обмена!", Toast.LENGTH_SHORT).show()
    }

    private fun copyAllLog() {
        if (currentLogText.isBlank()) {
            Toast.makeText(this, "Лог пуст", Toast.LENGTH_SHORT).show()
            return
        }
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("OpenGothic Full Log", currentLogText)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, "Весь лог скопирован (${currentLogText.length} символов)!", Toast.LENGTH_SHORT).show()
    }

    private fun shareLog() {
        if (currentLogText.isBlank()) {
            Toast.makeText(this, "Лог пуст", Toast.LENGTH_SHORT).show()
            return
        }
        val shareIntent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "OpenGothic Log - $crashSource")
            putExtra(Intent.EXTRA_TEXT, currentLogText)
        }
        startActivity(Intent.createChooser(shareIntent, "Поделиться логом"))
    }

    private fun restartGame() {
        val intent = Intent(this, GameActivity::class.java).apply {
            putExtra(GameActivity.EXTRA_GAME_DIR, gameDir)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        }
        startActivity(intent)
        finish()
    }

    private fun closeToLauncher() {
        val intent = Intent(this, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        }
        startActivity(intent)
        finish()
    }
}
