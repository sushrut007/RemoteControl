package com.darpan.host

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.content.Intent
import android.graphics.Path
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.DisplayMetrics
import android.util.Log
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import androidx.localbroadcastmanager.content.LocalBroadcastManager

class RemoteControlAccessibilityService : AccessibilityService() {

    private val mainHandler = Handler(Looper.getMainLooper())
    private var screenWidth = 1080
    private var screenHeight = 1920
    private var pointerX = 0f
    private var pointerY = 0f
    private var buttonDown = false

    override fun onServiceConnected() {
        super.onServiceConnected()
        instance = this
        updateMetrics()
        LocalBroadcastManager.getInstance(this)
            .sendBroadcast(Intent(ACTION_ACCESSIBILITY_READY))
        Log.i(TAG, "Accessibility service connected")
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) = Unit

    override fun onInterrupt() {
        Log.w(TAG, "Accessibility service interrupted")
    }

    override fun onDestroy() {
        if (instance === this) instance = null
        super.onDestroy()
    }

    fun handleMouse(ev: MouseEventPayload) {
        mainHandler.post {
            updateMetrics()
            pointerX = (ev.x.coerceIn(0f, 1f)) * screenWidth
            pointerY = (ev.y.coerceIn(0f, 1f)) * screenHeight

            when (ev.eventType) {
                "mousemove" -> if (buttonDown) dispatchDrag(pointerX, pointerY)
                "mousedown" -> {
                    buttonDown = true
                    dispatchTap(pointerX, pointerY, longPress = ev.button == "right")
                }
                "mouseup" -> buttonDown = false
                "wheel" -> dispatchScroll(pointerX, pointerY, ev.deltaY)
                "click" -> dispatchTap(pointerX, pointerY, longPress = false)
                else -> dispatchTap(pointerX, pointerY, longPress = false)
            }
        }
    }

    fun handleKeyboard(ev: KeyboardEventPayload) {
        mainHandler.post {
            val key = ev.key.lowercase()
            when {
                key == "back" || key == "escape" -> performGlobalAction(GLOBAL_ACTION_BACK)
                key == "home" -> performGlobalAction(GLOBAL_ACTION_HOME)
                key == "enter" || key == "return" -> performEnter()
                key == "tab" -> performTab()
                key.length == 1 && ev.eventType == "keydown" -> typeCharacter(key)
                key == "arrowleft" -> swipeDirection(+120, 0)
                key == "arrowright" -> swipeDirection(-120, 0)
                key == "arrowup" -> swipeDirection(0, +120)
                key == "arrowdown" -> swipeDirection(0, -120)
            }
        }
    }

    fun performBack() = performGlobalAction(GLOBAL_ACTION_BACK)
    fun performHome() = performGlobalAction(GLOBAL_ACTION_HOME)
    fun performRecents() = performGlobalAction(GLOBAL_ACTION_RECENTS)

    private fun dispatchTap(x: Float, y: Float, longPress: Boolean) {
        val path = Path().apply { moveTo(x, y) }
        val duration = if (longPress) 600L else 50L
        val stroke = GestureDescription.StrokeDescription(path, 0, duration)
        dispatchGesture(
            GestureDescription.Builder().addStroke(stroke).build(),
            null,
            null,
        )
    }

    private fun dispatchDrag(x: Float, y: Float) {
        val path = Path().apply {
            moveTo(pointerX, pointerY)
            lineTo(x, y)
        }
        val stroke = GestureDescription.StrokeDescription(path, 0, 40)
        dispatchGesture(
            GestureDescription.Builder().addStroke(stroke).build(),
            null,
            null,
        )
    }

    private fun dispatchScroll(x: Float, y: Float, deltaY: Float) {
        val dy = (-deltaY * 240f).coerceIn(-900f, 900f)
        val path = Path().apply {
            moveTo(x, y)
            lineTo(x, y + dy)
        }
        val stroke = GestureDescription.StrokeDescription(path, 0, 120)
        dispatchGesture(
            GestureDescription.Builder().addStroke(stroke).build(),
            null,
            null,
        )
    }

    private fun swipeDirection(dx: Int, dy: Int) {
        val cx = screenWidth / 2f
        val cy = screenHeight / 2f
        val path = Path().apply {
            moveTo(cx, cy)
            lineTo(cx + dx, cy + dy)
        }
        val stroke = GestureDescription.StrokeDescription(path, 0, 100)
        dispatchGesture(
            GestureDescription.Builder().addStroke(stroke).build(),
            null,
            null,
        )
    }

    private fun performEnter() {
        val focused = rootInActiveWindow?.findFocus(AccessibilityNodeInfo.FOCUS_INPUT)
        if (focused != null && focused.isEditable && Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            val current = focused.text?.toString().orEmpty()
            val args = Bundle()
            args.putCharSequence(
                AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE,
                current + "\n",
            )
            focused.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args)
            focused.recycle()
        } else {
            dispatchTap(pointerX, pointerY, longPress = false)
        }
    }

    private fun performTab() {
        val root = rootInActiveWindow ?: return
        val focusable = findFirstFocusable(root)
        focusable?.performAction(AccessibilityNodeInfo.ACTION_FOCUS)
        focusable?.recycle()
        root.recycle()
    }

    private fun typeCharacter(text: String) {
        val root = rootInActiveWindow ?: return
        val focused = root.findFocus(AccessibilityNodeInfo.FOCUS_INPUT)
        if (focused != null && focused.isEditable && Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            val current = focused.text?.toString().orEmpty()
            val args = Bundle()
            args.putCharSequence(
                AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE,
                current + text,
            )
            focused.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args)
            focused.recycle()
        }
        root.recycle()
    }

    private fun findFirstFocusable(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        if (node.isFocusable && node.isVisibleToUser) return AccessibilityNodeInfo.obtain(node)
        for (i in 0 until node.childCount) {
            val child = node.getChild(i) ?: continue
            val found = findFirstFocusable(child)
            child.recycle()
            if (found != null) return found
        }
        return null
    }

    private fun updateMetrics() {
        val metrics = DisplayMetrics()
        val windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
        @Suppress("DEPRECATION")
        windowManager.defaultDisplay.getRealMetrics(metrics)
        screenWidth = metrics.widthPixels.coerceAtLeast(1)
        screenHeight = metrics.heightPixels.coerceAtLeast(1)
    }

    companion object {
        const val ACTION_ACCESSIBILITY_READY = "com.darpan.host.ACCESSIBILITY_READY"
        private const val TAG = "RemoteControlA11y"

        @Volatile
        private var instance: RemoteControlAccessibilityService? = null

        fun getInstance(): RemoteControlAccessibilityService? = instance
        fun isEnabled(): Boolean = instance != null
    }
}
