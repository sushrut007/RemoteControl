package com.darpan.remote.ui

import android.content.Context
import android.util.AttributeSet
import android.view.MotionEvent
import androidx.appcompat.widget.AppCompatImageView
import com.darpan.remote.protocol.ControlProtocol

class RemoteScreenView
@JvmOverloads
constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : AppCompatImageView(context, attrs) {
    var streamWidth: Int = 0
    var streamHeight: Int = 0
    var controlEnabled: Boolean = false
    var onControlPayload: ((ByteArray) -> Unit)? = null

    private var lastNormX = 0
    private var lastNormY = 0

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!controlEnabled || streamWidth <= 0 || streamHeight <= 0) {
            return super.onTouchEvent(event)
        }
        val nx = normX(event.x, width.toFloat())
        val ny = normY(event.y, height.toFloat())
        lastNormX = nx
        lastNormY = ny
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_MOVE,
            -> send(ControlProtocol.packMouseMove(nx, ny))
            MotionEvent.ACTION_UP -> {
                send(ControlProtocol.packMouseMove(nx, ny))
                send(ControlProtocol.packMouseButton(0, true, nx, ny))
                send(ControlProtocol.packMouseButton(0, false, nx, ny))
            }
        }
        return true
    }

    fun sendClick(button: Int = 0) {
        if (!controlEnabled) return
        send(ControlProtocol.packMouseButton(button, true, lastNormX, lastNormY))
        send(ControlProtocol.packMouseButton(button, false, lastNormX, lastNormY))
    }

    private fun normX(touchX: Float, viewW: Float): Int {
        if (viewW <= 0f) return 0
        val contentW = streamWidth.toFloat()
        val scale = minOf(viewW / contentW, height.toFloat() / streamHeight)
        val drawnW = contentW * scale
        val pad = (viewW - drawnW) / 2f
        val rel = ((touchX - pad) / drawnW).coerceIn(0f, 1f)
        return ControlProtocol.normalize(rel, 65535)
    }

    private fun normY(touchY: Float, viewH: Float): Int {
        if (viewH <= 0f) return 0
        val contentH = streamHeight.toFloat()
        val scale = minOf(width.toFloat() / streamWidth, viewH / contentH)
        val drawnH = contentH * scale
        val pad = (viewH - drawnH) / 2f
        val rel = ((touchY - pad) / drawnH).coerceIn(0f, 1f)
        return ControlProtocol.normalize(rel, 65535)
    }

    private fun send(payload: ByteArray) {
        onControlPayload?.invoke(payload)
    }
}
