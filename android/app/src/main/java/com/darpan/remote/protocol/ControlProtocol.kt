package com.darpan.remote.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

object ControlProtocol {
    const val VERSION: Byte = 0x01
    const val MOUSE_MOVE: Byte = 0x10
    const val MOUSE_BUTTON: Byte = 0x11
    const val MOUSE_WHEEL: Byte = 0x12
    const val KEY_DOWN: Byte = 0x20
    const val KEY_UP: Byte = 0x21

    private fun header(type: Byte): ByteArray = byteArrayOf(VERSION, type)

    fun packMouseMove(nx: Int, ny: Int): ByteArray {
        val buf = ByteBuffer.allocate(6).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(header(MOUSE_MOVE))
        buf.putShort(nx.toShort())
        buf.putShort(ny.toShort())
        return buf.array()
    }

    fun packMouseButton(button: Int, down: Boolean, nx: Int, ny: Int): ByteArray {
        val buf = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(header(MOUSE_BUTTON))
        buf.put(button.toByte())
        buf.put(if (down) 1 else 0)
        buf.putShort(nx.toShort())
        buf.putShort(ny.toShort())
        return buf.array()
    }

    fun packMouseWheel(delta: Short, nx: Int, ny: Int): ByteArray {
        val buf = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(header(MOUSE_WHEEL))
        buf.putShort(delta)
        buf.putShort(nx.toShort())
        buf.putShort(ny.toShort())
        return buf.array()
    }

    fun packKey(down: Boolean, vk: Int, extended: Boolean): ByteArray {
        val buf = ByteBuffer.allocate(6).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(header(if (down) KEY_DOWN else KEY_UP))
        buf.putShort(vk.toShort())
        buf.putShort(if (extended) 1 else 0)
        return buf.array()
    }

    fun normalize(value: Float, max: Int): Int {
        if (max <= 0) return 0
        return (value.coerceIn(0f, 1f) * 65535f).toInt().coerceIn(0, 65535)
    }
}
