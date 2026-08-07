package com.darpan.host

import android.app.Activity
import android.content.Intent

/**
 * MediaProjection result data must be consumed in-process immediately.
 * Passing the result [Intent] through a Service extra often crashes on Android 10+.
 */
object MediaProjectionHolder {
    @Volatile
    private var resultCode: Int = Activity.RESULT_CANCELED

    @Volatile
    private var resultData: Intent? = null

    fun set(code: Int, data: Intent) {
        resultCode = code
        resultData = Intent(data)
    }

    fun consume(): Pair<Int, Intent>? {
        val data = resultData ?: return null
        val code = resultCode
        clear()
        return code to data
    }

    fun clear() {
        resultCode = Activity.RESULT_CANCELED
        resultData = null
    }
}
