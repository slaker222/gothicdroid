package com.opengothic

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.view.*
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "OpenGothic"
        private const val REQUEST_STORAGE_PERMISSION = 1001

        init {
            Log.i(TAG, "Loading libc++_shared.so...")
            try {
                System.loadLibrary("c++_shared")
                Log.i(TAG, "libc++_shared.so loaded successfully")
            } catch (e: UnsatisfiedLinkError) {
                Log.w(TAG, "libc++_shared already loaded or system provided: ${e.message}")
            }

            Log.i(TAG, "Loading libopengothic.so...")
            System.loadLibrary("opengothic")
            Log.i(TAG, "libopengothic.so loaded successfully")
        }
    }

    private external fun nativeSetSurface(surface: Surface, gameDir: String)
    private external fun nativeSurfaceChanged(width: Int, height: Int)
    private external fun nativeDestroySurface()
    private external fun nativeOnTouch(action: Int, x: Float, y: Float, id: Int)
    private external fun nativeOnKey(action: Int, keyCode: Int)

    private lateinit var surfaceView: SurfaceView
    private var surfaceHolder: SurfaceHolder? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "MainActivity onCreate called")

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        hideSystemBars()

        checkAndRequestStoragePermissions()

        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        surfaceView.isFocusable = true
        surfaceView.isFocusableInTouchMode = true
        surfaceView.requestFocus()

        surfaceView.setOnTouchListener { _, event ->
            val action = event.actionMasked
            val index = event.actionIndex
            val pointerId = event.getPointerId(index)
            val x = event.getX(index)
            val y = event.getY(index)
            nativeOnTouch(action, x, y, pointerId)
            true
        }

        setContentView(surfaceView)
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
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

    private fun checkAndRequestStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Log.i(TAG, "Requesting MANAGE_APP_ALL_FILES_ACCESS_PERMISSION")
                Toast.makeText(
                    this,
                    "Для загрузки ресурсов Gothic II требуется доступ к файлам",
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
        val defaultPath = "/sdcard/Gothic2"
        Log.i(TAG, "Default game directory: $defaultPath")
        return defaultPath
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.i(TAG, "surfaceCreated: surface is valid = ${holder.surface.isValid}")
        surfaceHolder = holder
        val gameDir = findGameDirectory()
        nativeSetSurface(holder.surface, gameDir)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.i(TAG, "surfaceChanged: width=$width, height=$height, format=$format")
        nativeSurfaceChanged(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.i(TAG, "surfaceDestroyed")
        nativeDestroySurface()
        surfaceHolder = null
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            nativeOnKey(0, keyCode)
            return true
        }
        nativeOnKey(0, keyCode)
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            nativeOnKey(1, keyCode)
            return true
        }
        nativeOnKey(1, keyCode)
        return super.onKeyUp(keyCode, event)
    }
}
