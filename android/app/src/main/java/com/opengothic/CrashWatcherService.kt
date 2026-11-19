package com.opengothic

import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.util.Log
import java.io.File

class CrashWatcherService : Service() {

    companion object {
        private const val TAG = "CrashWatcher"
    }

    inner class CrashWatcherBinder : Binder() {
        fun watch(clientToken: IBinder, gameDir: String) {
            try {
                clientToken.linkToDeath({
                    Log.w(TAG, "Monitored game process died. Checking crash markers for $gameDir...")
                    handleGameProcessDied(gameDir)
                }, 0)
                Log.i(TAG, "Attached death recipient for $gameDir")
            } catch (e: Exception) {
                Log.e(TAG, "Failed to linkToDeath: ${e.message}")
            }
        }
    }

    private val binder = CrashWatcherBinder()

    override fun onBind(intent: Intent?): IBinder {
        return binder
    }

    private fun handleGameProcessDied(dir: String) {
        // Wait 250ms for C++ signal handler to flush crash.log and crash_marker.txt
        try { Thread.sleep(250) } catch (e: Exception) {}

        val gdir = File(dir)
        val normalExit = File(gdir, "normal_exit.marker")
        val resumeMark = File(gdir, "resume.marker")

        if (normalExit.exists() || resumeMark.exists()) {
            Log.i(TAG, "Normal exit or resume detected; no crash window.")
            normalExit.delete()
            File(gdir, "game_running.marker").delete()
            return
        }

        val crashMarker = File(gdir, "crash_marker.txt")
        if (!crashMarker.exists()) {
            Log.i(TAG, "Process died without crash marker (user closed / killed by OS). No crash window.")
            File(gdir, "game_running.marker").delete()
            return
        }

        val r = try { crashMarker.readText().trim() } catch (e: Exception) { "" }
        crashMarker.delete()
        File(gdir, "game_running.marker").delete()
        val crashReason = if (r.isNotBlank()) r else "Game Process Crashed"

        Log.w(TAG, "Launching CrashActivity due to crash: $crashReason")
        // 1. Ensure MainActivity (launcher) is active in background
        val mainIntent = Intent(this, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        startActivity(mainIntent)

        // 2. Open CrashActivity dialog on top of launcher
        val crashIntent = Intent(this, CrashActivity::class.java).apply {
            putExtra(CrashActivity.EXTRA_GAME_DIR, dir)
            putExtra(CrashActivity.EXTRA_CRASH_SOURCE, crashReason)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        }
        startActivity(crashIntent)
    }
}
