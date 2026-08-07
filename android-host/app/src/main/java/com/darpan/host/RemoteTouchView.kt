package com.darpan.host

import android.content.Context
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import androidx.constraintlayout.widget.ConstraintLayout

class RemoteTouchView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : ConstraintLayout(context, attrs) {

    var onTouchEventNormalized: ((
        x: Float,
        y: Float,
        eventType: String,
        button: String,
    ) -> Unit)? = null

    init {
        isClickable = true
        setOnTouchListener(TouchListener())
    }

    private inner class TouchListener : OnTouchListener {
        private var down = false

        override fun onTouch(v: View, event: MotionEvent): Boolean {
            val nx = (event.x / width.toFloat()).coerceIn(0f, 1f)
            val ny = (event.y / height.toFloat()).coerceIn(0f, 1f)
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    down = true
                    onTouchEventNormalized?.invoke(nx, ny, "mousedown", "left")
                }
                MotionEvent.ACTION_MOVE -> {
                    if (down) {
                        onTouchEventNormalized?.invoke(nx, ny, "mousemove", "left")
                    }
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    onTouchEventNormalized?.invoke(nx, ny, "mouseup", "left")
                    down = false
                }
            }
            return true
        }
    }
}
