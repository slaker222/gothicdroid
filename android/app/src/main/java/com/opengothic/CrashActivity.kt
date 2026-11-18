package com.opengothic

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.os.Bundle
import android.text.Editable
import android.text.TextUtils
import android.text.TextWatcher
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

    private lateinit var tvLog: TextView
    private lateinit var svLog: ScrollView
    private lateinit var spLogs: Spinner
    private lateinit var etFilter: EditText

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

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        gameDir = intent?.getStringExtra(EXTRA_GAME_DIR) ?: "/sdcard/Gothic2"
        crashSource = intent?.getStringExtra(EXTRA_CRASH_SOURCE) ?: "Unknown Crash"
        crashDetails = intent?.getStringExtra(EXTRA_CRASH_DETAILS)
        loadLogList()
    }

    private fun dp(v: Int): Int {
        return TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics
        ).toInt()
    }

    private fun makeButton(text: String, onClick: () -> Unit): Button {
        val btn = Button(this).apply {
            this.text = text
            isAllCaps = false
            setTextColor(Color.parseColor("#eeeeee"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f)
            typeface = Typeface.SANS_SERIF
            minHeight = 0
            minimumHeight = dp(32)
            setPadding(dp(12), 0, dp(12), 0)

            val normal = GradientDrawable().apply {
                setColor(Color.parseColor("#2a2a30"))
                setStroke(dp(1), Color.parseColor("#444444"))
                cornerRadius = dp(6).toFloat()
            }
            val pressed = GradientDrawable().apply {
                setColor(Color.parseColor("#3a3a42"))
                setStroke(dp(1), Color.parseColor("#555555"))
                cornerRadius = dp(6).toFloat()
            }
            background = StateListDrawable().apply {
                addState(intArrayOf(android.R.attr.state_pressed), pressed)
                addState(intArrayOf(), normal)
            }
            setOnClickListener { onClick() }
        }
        return btn
    }

    private class DarkSpinnerAdapter(
        context: Context,
        private val items: List<String>
    ) : ArrayAdapter<String>(context, 0, items) {

        private fun createItemView(position: Int, convertView: View?, parent: ViewGroup, isDropDown: Boolean): View {
            val tv = (convertView as? TextView) ?: TextView(context).apply {
                setTextColor(Color.parseColor("#eeeeee"))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f)
                typeface = Typeface.SANS_SERIF
                setSingleLine(true)
                ellipsize = TextUtils.TruncateAt.MIDDLE
            }
            val density = context.resources.displayMetrics.density
            val padH = (10 * density).toInt()
            val padV = if (isDropDown) (10 * density).toInt() else (4 * density).toInt()
            tv.setPadding(padH, padV, padH, padV)
            if (isDropDown) {
                tv.setBackgroundColor(Color.parseColor("#18181c"))
            }
            tv.text = items[position]
            return tv
        }

        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            return createItemView(position, convertView, parent, false)
        }

        override fun getDropDownView(position: Int, convertView: View?, parent: ViewGroup): View {
            return createItemView(position, convertView, parent, true)
        }
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.parseColor("#0b0b0d"))
            fitsSystemWindows = false
            setOnApplyWindowInsetsListener { _, insets ->
                setPadding(
                    insets.systemWindowInsetLeft,
                    insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight,
                    insets.systemWindowInsetBottom
                )
                insets
            }
        }

        // --- 1. Top Bar (Toolbar matching #log-ov .lg-bar) ---
        val barScroll = HorizontalScrollView(this).apply {
            isHorizontalScrollBarEnabled = false
            isFillViewport = true
            setBackgroundColor(Color.parseColor("#18181c"))
        }

        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(10), dp(6), dp(10), dp(6))
        }

        // Title "Crash Log"
        val tvBarTitle = TextView(this).apply {
            text = "Crash Log"
            setTextColor(Color.parseColor("#eeeeee"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            typeface = Typeface.DEFAULT_BOLD
            setPadding(0, 0, dp(10), 0)
        }
        bar.addView(tvBarTitle)

        // Spinner (Log file selector)
        spLogs = Spinner(this).apply {
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#2a2a30"))
                setStroke(dp(1), Color.parseColor("#444444"))
                cornerRadius = dp(6).toFloat()
            }
            setPadding(dp(4), 0, dp(4), 0)
            setPopupBackgroundDrawable(GradientDrawable().apply {
                setColor(Color.parseColor("#18181c"))
                setStroke(dp(1), Color.parseColor("#444444"))
                cornerRadius = dp(6).toFloat()
            })
        }
        val spParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(32)).apply {
            marginEnd = dp(8)
        }
        bar.addView(spLogs, spParams)

        // Filter Input
        etFilter = EditText(this).apply {
            hint = "Filter"
            setHintTextColor(Color.parseColor("#777777"))
            setTextColor(Color.parseColor("#eeeeee"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f)
            typeface = Typeface.SANS_SERIF
            setSingleLine(true)
            minHeight = 0
            minimumHeight = dp(32)
            setPadding(dp(8), dp(2), dp(8), dp(2))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#0b0b0d"))
                setStroke(dp(1), Color.parseColor("#444444"))
                cornerRadius = dp(6).toFloat()
            }
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                    applyFilter(s?.toString().orEmpty())
                }
                override fun afterTextChanged(s: Editable?) {}
            })
        }
        val filterParams = LinearLayout.LayoutParams(dp(130), dp(32)).apply {
            marginEnd = dp(8)
        }
        bar.addView(etFilter, filterParams)

        // Buttons
        val btnParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(32)).apply {
            marginEnd = dp(6)
        }

        val btnCopy500 = makeButton("Copy 500") { copyLast500Lines() }
        bar.addView(btnCopy500, btnParams)

        val btnCopyAll = makeButton("Copy full") { copyAllLog() }
        bar.addView(btnCopyAll, btnParams)

        val btnRestart = makeButton("Restart") { restartGame() }
        bar.addView(btnRestart, btnParams)

        val btnClose = makeButton("Close") { closeToLauncher() }
        bar.addView(btnClose, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(32)))

        barScroll.addView(bar)
        root.addView(barScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        // Divider below toolbar (1px #333333)
        val div1 = View(this).apply {
            setBackgroundColor(Color.parseColor("#333333"))
        }
        root.addView(div1, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)))

        // --- 2. Compact Info Bar ---
        val infoBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(Color.parseColor("#121215"))
            setPadding(dp(10), dp(3), dp(10), dp(3))
        }

        val timeStr = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
        val tvInfo = TextView(this).apply {
            text = "Reason: $crashSource   |   Time: $timeStr   |   Dir: $gameDir"
            setTextColor(Color.parseColor("#888d96"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
            setSingleLine(true)
            ellipsize = TextUtils.TruncateAt.END
        }
        infoBar.addView(tvInfo)
        root.addView(infoBar, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        // Divider below info bar (1px #222226)
        val div2 = View(this).apply {
            setBackgroundColor(Color.parseColor("#222226"))
        }
        root.addView(div2, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)))

        // --- 3. Monospace Log Viewer ---
        svLog = ScrollView(this).apply {
            setBackgroundColor(Color.parseColor("#0b0b0d"))
            setPadding(dp(8), dp(6), dp(8), dp(6))
        }

        tvLog = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setLineSpacing(0f, 1.3f)
            setTextColor(Color.parseColor("#cfd2d6"))
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

        // 1. Primary: Detailed session log opengothic_log.txt
        val sessionLog = File(dir, "opengothic_log.txt")
        if (sessionLog.exists() && sessionLog.length() > 0) {
            logEntries.add(LogEntry("opengothic_log.txt (full log)", sessionLog, false))
        }

        // 2. Current crash stacktrace
        val crashLog = File(dir, "crash.log")
        if (crashLog.exists() && crashLog.length() > 0) {
            logEntries.add(LogEntry("crash.log (stacktrace only)", crashLog, true))
        }

        // 3. Historical session logs (opengothic_log_1.txt .. opengothic_log_5.txt)
        for (i in 1..LogManager.MAX_LOGS) {
            val f = File(dir, "opengothic_log_$i.txt")
            if (f.exists() && f.length() > 0) {
                logEntries.add(LogEntry("opengothic_log_$i.txt (session #$i)", f, false))
            }
        }

        // 4. Historical crash logs (crash_1.log .. crash_5.log)
        for (i in 1..LogManager.MAX_LOGS) {
            val f = File(dir, "crash_$i.log")
            if (f.exists() && f.length() > 0) {
                logEntries.add(LogEntry("crash_$i.log (crash #$i)", f, true))
            }
        }

        // 5. Java crash log in filesDir
        val javaCrash = File(filesDir, "last_crash.txt")
        if (javaCrash.exists() && javaCrash.length() > 0) {
            logEntries.add(LogEntry("last_crash.txt (Java crash)", javaCrash, true))
        }

        if (logEntries.isEmpty()) {
            val dummyText = if (!crashDetails.isNullOrBlank()) {
                "=== CRASH DETAILS ===\n$crashDetails"
            } else {
                "No log files found in $gameDir"
            }
            currentLogText = dummyText
            tvLog.text = dummyText
            return
        }

        val labels = logEntries.map { it.label }
        val adapter = DarkSpinnerAdapter(this, labels)
        spLogs.adapter = adapter

        spLogs.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (position in logEntries.indices) {
                    displayLogFile(logEntries[position].file)
                }
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        // Default to first item (detailed session log)
        displayLogFile(logEntries[0].file)
    }

    private fun displayLogFile(file: File) {
        if (!file.exists()) {
            tvLog.text = "File ${file.name} does not exist."
            currentLogText = ""
            return
        }

        try {
            var text = file.readText()
            if (file.name.startsWith("opengothic_log")) {
                // If viewing session log, append corresponding crash stacktrace if available
                val crashFile = if (file.name == "opengothic_log.txt") {
                    File(file.parentFile, "crash.log")
                } else {
                    val idx = file.name.substringAfter("opengothic_log_").substringBefore(".txt")
                    File(file.parentFile, "crash_$idx.log")
                }
                if (crashFile.exists() && crashFile.length() > 0) {
                    val crashText = crashFile.readText().trim()
                    if (crashText.isNotEmpty()) {
                        text = text.trimEnd() + "\n\n" +
                               "=================== CRASH STACKTRACE ===================\n" +
                               crashText + "\n" +
                               "=======================================================\n"
                    }
                }
            }
            currentLogText = text
            applyFilter(etFilter.text?.toString().orEmpty())
        } catch (e: Exception) {
            tvLog.text = "Error reading ${file.name}: ${e.message}"
            currentLogText = ""
        }
    }

    private fun applyFilter(query: String) {
        val q = query.trim().lowercase(Locale.ROOT)
        if (q.isEmpty()) {
            renderLog(currentLogText)
            return
        }
        val filtered = currentLogText.lineSequence()
            .filter { it.contains(q, ignoreCase = true) }
            .joinToString("\n")
        renderLog(filtered)
    }

    private fun renderLog(text: String) {
        if (text.length > 400000) {
            val banner = "...\n[Log display truncated (${text.length / 1024} KB). Full log is preserved for Copy/Share]\n\n"
            tvLog.text = banner + text.substring(text.length - 400000)
        } else {
            tvLog.text = text
        }
        svLog.post { svLog.fullScroll(View.FOCUS_DOWN) }
    }

    private fun copyLast500Lines() {
        if (currentLogText.isBlank()) {
            Toast.makeText(this, "Log is empty", Toast.LENGTH_SHORT).show()
            return
        }
        val lines = currentLogText.lines()
        val count = minOf(500, lines.size)
        val tail = lines.takeLast(count).joinToString("\n")

        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("OpenGothic Log Tail", tail)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, "Copied last $count lines to clipboard", Toast.LENGTH_SHORT).show()
    }

    private fun copyAllLog() {
        if (currentLogText.isBlank()) {
            Toast.makeText(this, "Log is empty", Toast.LENGTH_SHORT).show()
            return
        }
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("OpenGothic Full Log", currentLogText)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, "Copied full log (${currentLogText.length} chars)", Toast.LENGTH_SHORT).show()
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
