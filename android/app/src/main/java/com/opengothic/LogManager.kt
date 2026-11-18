package com.opengothic

import java.io.File

object LogManager {
    const val MAX_LOGS = 5 // Keeps at least 5 historical logs + 1 active

    fun rotateLogs(gameDir: File) {
        if (!gameDir.exists() || !gameDir.isDirectory) return

        // 1. Rotate opengothic_log.txt -> opengothic_log_1.txt ... opengothic_log_5.txt
        rotateFile(gameDir, "opengothic_log", ".txt", MAX_LOGS)

        // 2. Rotate crash.log -> crash_1.log ... crash_5.log
        rotateFile(gameDir, "crash", ".log", MAX_LOGS)
    }

    private fun rotateFile(dir: File, baseName: String, ext: String, max: Int) {
        val cur = File(dir, "$baseName$ext")
        if (!cur.exists() || cur.length() == 0L) return

        val oldest = File(dir, "${baseName}_$max$ext")
        if (oldest.exists()) oldest.delete()

        for (i in (max - 1) downTo 1) {
            val src = File(dir, "${baseName}_$i$ext")
            val dst = File(dir, "${baseName}_${i + 1}$ext")
            if (src.exists()) {
                if (dst.exists()) dst.delete()
                src.renameTo(dst)
            }
        }

        val dst1 = File(dir, "${baseName}_1$ext")
        if (dst1.exists()) dst1.delete()
        cur.renameTo(dst1)
    }
}
