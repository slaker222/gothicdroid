package com.opengothic

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import java.io.File

/**
 * Downloads a game in a foreground service: the download goes on when the app is minimized or the screen is off,
 * and the notification shows the progress (with a Cancel button). The state of the download is in [current] /
 * [game], the launcher page reads it from there (MainActivity.dlState). An interrupted download is resumed by the
 * next start: PackDownloader keeps the list of the chunks that are done.
 */
class DownloadService : Service() {
    companion object {
        private const val TAG = "OpenGothic"
        private const val CHANNEL = "download"
        private const val ID_PROGRESS = 41
        private const val ID_RESULT = 42
        const val ACTION_CANCEL = "com.opengothic.DOWNLOAD_CANCEL"
        const val EXTRA_SERVER = "server"
        const val EXTRA_GAME = "game"
        const val EXTRA_TEXT = "text"
        const val EXTRA_VOICE = "voice"
        const val EXTRA_TARGET = "target"

        @Volatile var current: PackDownloader? = null
        @Volatile var game = 0

        fun running() = current?.running == true

        /** starts the download; false when one is running already */
        fun start(ctx: Context, server: String, id: Int, text: String, voice: String, target: File): Boolean {
            if (running()) return false
            // the state is there before the service starts: the page asks at once
            val d = PackDownloader(server)
            d.running = true
            current = d
            game = id
            val i = Intent(ctx, DownloadService::class.java)
                .putExtra(EXTRA_SERVER, server).putExtra(EXTRA_GAME, id)
                .putExtra(EXTRA_TEXT, text).putExtra(EXTRA_VOICE, voice).putExtra(EXTRA_TARGET, target.path)
            try {
                androidx.core.content.ContextCompat.startForegroundService(ctx, i)
            } catch (e: Exception) {
                // the system does not allow a foreground service now: the download still runs in the process
                Log.w(TAG, "download service: ${e.message}")
                d.running = false
                return runPlain(d, id, text, voice, target)
            }
            return true
        }

        private fun runPlain(d: PackDownloader, id: Int, text: String, voice: String, target: File): Boolean {
            Thread { d.install(id, text, voice, target) }.start()
            return true
        }
    }

    private var wake: PowerManager.WakeLock? = null
    private var worker: Thread? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_CANCEL) {
            current?.cancel()
            return START_NOT_STICKY
        }
        val d = current
        if (intent == null || d == null || worker != null) {
            if (worker == null) stopSelf()
            return START_NOT_STICKY
        }
        val id = intent.getIntExtra(EXTRA_GAME, 1)
        val text = intent.getStringExtra(EXTRA_TEXT) ?: ""
        val voice = intent.getStringExtra(EXTRA_VOICE) ?: ""
        val target = File(intent.getStringExtra(EXTRA_TARGET) ?: return START_NOT_STICKY)

        makeChannel()
        val n = notification(id, 0, 0, "")
        if (Build.VERSION.SDK_INT >= 34) startForeground(ID_PROGRESS, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        else startForeground(ID_PROGRESS, n)

        wake = (getSystemService(Context.POWER_SERVICE) as PowerManager)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "gothicdroid:download").also { it.acquire(6 * 3600 * 1000L) }

        worker = Thread {
            val nm = getSystemService(NotificationManager::class.java)
            val ticker = Thread {
                while (d.running) {
                    try { nm.notify(ID_PROGRESS, notification(id, d.done.get(), d.total, d.phase)) } catch (e: Exception) {}
                    try { Thread.sleep(1000) } catch (e: InterruptedException) { return@Thread }
                }
            }.also { it.isDaemon = true; it.start() }
            d.install(id, text, voice, target)
            ticker.interrupt()
            if (d.finished) {
                // the game is in the folder of the app now: the next start uses it
                getSharedPreferences("opengothic_launcher", Context.MODE_PRIVATE).edit().putString("path_$id", target.path).apply()
            }
            stopForeground(STOP_FOREGROUND_REMOVE)
            result(id, d)
            wake?.let { if (it.isHeld) it.release() }
            worker = null
            stopSelf()
        }.also { it.start() }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        wake?.let { if (it.isHeld) it.release() }
        super.onDestroy()
    }

    // the app is swiped out of the recent apps: the download stops with the process, the chunks that are done stay
    override fun onTaskRemoved(rootIntent: Intent?) {
        current?.cancel()
        super.onTaskRemoved(rootIntent)
    }

    private fun title(id: Int) = "GothicDroid: Gothic " + id

    private fun openApp(): PendingIntent = PendingIntent.getActivity(
        this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)

    private fun notification(id: Int, done: Long, total: Long, phase: String): Notification {
        val cancel = PendingIntent.getService(this, 1, Intent(this, DownloadService::class.java).setAction(ACTION_CANCEL),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val b = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(title(id))
            .setContentIntent(openApp())
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .addAction(0, "Cancel", cancel)
        if (total > 0) {
            val pct = (done * 100 / total).toInt().coerceIn(0, 100)
            b.setProgress(100, pct, false).setContentText("$pct%  ${done / 1048576} / ${total / 1048576} MB")
        } else {
            b.setProgress(0, 0, true).setContentText(if (phase == "check") "Checking…" else "Starting…")
        }
        return b.build()
    }

    private fun result(id: Int, d: PackDownloader) {
        val text = when {
            d.finished -> "The game is installed"
            d.error == "cancelled" -> "Cancelled: the part that is done is kept, the next start goes on"
            else -> "Download failed: ${d.error}"
        }
        val n = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(if (d.finished) android.R.drawable.stat_sys_download_done else android.R.drawable.stat_notify_error)
            .setContentTitle(title(id))
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(openApp())
            .setAutoCancel(true)
            .build()
        try { getSystemService(NotificationManager::class.java).notify(ID_RESULT, n) } catch (e: Exception) { Log.w(TAG, "notify: ${e.message}") }
    }

    private fun makeChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            getSystemService(NotificationManager::class.java)
                .createNotificationChannel(NotificationChannel(CHANNEL, "Game download", NotificationManager.IMPORTANCE_LOW))
        }
    }
}
