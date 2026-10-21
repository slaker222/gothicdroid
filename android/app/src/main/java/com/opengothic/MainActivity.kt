package com.opengothic

import android.os.Bundle
import android.util.Log
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    companion object {
        private const val TAG = "OpenGothic"

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
            Log.i(TAG, "libopengothic.so loaded successfully via System.loadLibrary")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        Log.i(TAG, "MainActivity onCreate called")
        try {
            super.onCreate(savedInstanceState)
            Log.i(TAG, "GameActivity super.onCreate finished")
        } catch (t: Throwable) {
            Log.w(TAG, "GameActivity native glue bypassed for Stage 1: ${t.message}")
        }
    }
}
