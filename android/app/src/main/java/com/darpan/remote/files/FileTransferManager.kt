package com.darpan.remote.files

import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

class FileTransferManager {

    interface Listener {
        fun onIncomingOffer(transferId: Long, fileName: String, size: Long, sha256: ByteArray)
        fun onTransferStarted(transferId: Long, fileName: String, size: Long, isOutgoing: Boolean)
        fun onTransferProgress(
            transferId: Long,
            transferred: Long,
            total: Long,
            speedBps: Double,
            elapsedSec: Long,
        )
        fun onTransferFinished(transferId: Long, success: Boolean, message: String)
    }

    var listener: Listener? = null
    var sendBinary: ((ByteArray) -> Unit)? = null

    private val mainHandler = Handler(Looper.getMainLooper())
    private val ioExecutor = Executors.newCachedThreadPool()

    private var nextTransferId = 1L

    private class ActiveSend(
        val id: Long,
        val fileName: String,
        val inputStream: InputStream,
        val size: Long,
        val sha256: ByteArray,
        val startTimeMs: Long = System.currentTimeMillis(),
    ) {
        var offset: Long = 0
        val isCancelled = AtomicBoolean(false)
    }

    private class ActiveRecv(
        val id: Long,
        val fileName: String,
        val size: Long,
        val expectedSha256: ByteArray,
        val startTimeMs: Long = System.currentTimeMillis(),
    ) {
        var outputStream: OutputStream? = null
        var received: Long = 0
        val hasher: MessageDigest = MessageDigest.getInstance("SHA-256")
        val isCancelled = AtomicBoolean(false)
    }

    private val activeSends = ConcurrentHashMap<Long, ActiveSend>()
    private val activeRecvs = ConcurrentHashMap<Long, ActiveRecv>()

    fun sendFile(fileName: String, size: Long, inputStream: InputStream) {
        ioExecutor.execute {
            try {
                val id = synchronized(this) { nextTransferId++ }

                // Read all bytes or compute sha256 in memory/buffered
                // To support large files efficiently, read stream while computing sha256
                val digest = MessageDigest.getInstance("SHA-256")
                val tempBytes = ByteArrayOutputStream()
                val buffer = ByteArray(CHUNK_SIZE)
                var read: Int
                while (inputStream.read(buffer).also { read = it } != -1) {
                    tempBytes.write(buffer, 0, read)
                    digest.update(buffer, 0, read)
                }
                val allData = tempBytes.toByteArray()
                val sha = digest.digest()

                val send = ActiveSend(
                    id = id,
                    fileName = fileName,
                    inputStream = allData.inputStream(),
                    size = allData.size.toLong(),
                    sha256 = sha,
                )
                activeSends[id] = send

                val nameBytes = fileName.toByteArray(Charsets.UTF_8)
                val offer = ByteArrayOutputStream()
                offer.write(VERSION.toInt())
                offer.write(TYPE_OFFER.toInt())
                appendU32(offer, id)
                appendU64(offer, send.size)
                offer.write(nameBytes.size.coerceAtMost(255))
                offer.write(nameBytes, 0, nameBytes.size.coerceAtMost(255))
                offer.write(sha)

                mainHandler.post {
                    listener?.onTransferStarted(id, fileName, send.size, isOutgoing = true)
                }

                sendBinary?.invoke(offer.toByteArray())
            } catch (e: Exception) {
                mainHandler.post {
                    listener?.onTransferFinished(0, false, e.message ?: "Failed to open file")
                }
            }
        }
    }

    fun acceptIncoming(transferId: Long, outputStream: OutputStream) {
        val recv = activeRecvs[transferId] ?: return
        recv.outputStream = outputStream

        val accept = ByteArrayOutputStream()
        accept.write(VERSION.toInt())
        accept.write(TYPE_ACCEPT.toInt())
        appendU32(accept, transferId)
        sendBinary?.invoke(accept.toByteArray())
    }

    fun rejectIncoming(transferId: Long) {
        val recv = activeRecvs.remove(transferId)
        try {
            recv?.outputStream?.close()
        } catch (_: Exception) {}

        val reject = ByteArrayOutputStream()
        reject.write(VERSION.toInt())
        reject.write(TYPE_REJECT.toInt())
        appendU32(reject, transferId)
        sendBinary?.invoke(reject.toByteArray())

        mainHandler.post {
            listener?.onTransferFinished(transferId, false, "Declined")
        }
    }

    fun cancelTransfer(transferId: Long) {
        activeSends.remove(transferId)?.let { send ->
            send.isCancelled.set(true)
            try { send.inputStream.close() } catch (_: Exception) {}
            mainHandler.post {
                listener?.onTransferFinished(transferId, false, "Cancelled")
            }
        }

        activeRecvs.remove(transferId)?.let { recv ->
            recv.isCancelled.set(true)
            try { recv.outputStream?.close() } catch (_: Exception) {}
            mainHandler.post {
                listener?.onTransferFinished(transferId, false, "Cancelled")
            }
        }

        val cancel = ByteArrayOutputStream()
        cancel.write(VERSION.toInt())
        cancel.write(TYPE_CANCEL.toInt())
        appendU32(cancel, transferId)
        sendBinary?.invoke(cancel.toByteArray())
    }

    fun handleBinary(payload: ByteArray) {
        if (payload.size < 2 || payload[0] != VERSION) return
        val type = payload[1]
        val body = payload.copyOfRange(2, payload.size)

        when (type) {
            TYPE_OFFER -> {
                if (body.size < 13) return
                val id = readU32(body, 0)
                val size = readU64(body, 4)
                val nameLen = body[12].toInt() and 0xFF
                if (body.size < 13 + nameLen + 32) return
                val name = String(body, 13, nameLen, Charsets.UTF_8)
                val sha = body.copyOfRange(13 + nameLen, 13 + nameLen + 32)

                val recv = ActiveRecv(id, name, size, sha)
                activeRecvs[id] = recv

                mainHandler.post {
                    listener?.onIncomingOffer(id, name, size, sha)
                    listener?.onTransferStarted(id, name, size, isOutgoing = false)
                }
            }

            TYPE_ACCEPT -> {
                if (body.size < 4) return
                val id = readU32(body, 0)
                val send = activeSends[id] ?: return
                startPumpingSend(send)
            }

            TYPE_REJECT -> {
                if (body.size < 4) return
                val id = readU32(body, 0)
                activeSends.remove(id)?.let { send ->
                    try { send.inputStream.close() } catch (_: Exception) {}
                    mainHandler.post {
                        listener?.onTransferFinished(id, false, "Peer declined transfer")
                    }
                }
            }

            TYPE_CHUNK -> {
                if (body.size < 16) return
                val id = readU32(body, 0)
                val offset = readU64(body, 4)
                val len = readU32(body, 12).toInt()
                if (body.size < 16 + len) return
                val data = body.copyOfRange(16, 16 + len)

                val recv = activeRecvs[id] ?: return
                if (recv.isCancelled.get()) return

                try {
                    recv.outputStream?.write(data)
                    recv.hasher.update(data)
                    recv.received += len

                    val now = System.currentTimeMillis()
                    val elapsedMs = (now - recv.startTimeMs).coerceAtLeast(1)
                    val speedBps = (recv.received.toDouble() / (elapsedMs.toDouble() / 1000.0))

                    mainHandler.post {
                        listener?.onTransferProgress(
                            id,
                            recv.received,
                            recv.size,
                            speedBps,
                            elapsedMs / 1000,
                        )
                    }
                } catch (e: Exception) {
                    activeRecvs.remove(id)
                    try { recv.outputStream?.close() } catch (_: Exception) {}
                    mainHandler.post {
                        listener?.onTransferFinished(id, false, e.message ?: "Write error")
                    }
                }
            }

            TYPE_COMPLETE -> {
                if (body.size < 36) return
                val id = readU32(body, 0)
                val remoteSha = body.copyOfRange(4, 36)
                val recv = activeRecvs.remove(id) ?: return

                try {
                    recv.outputStream?.flush()
                    recv.outputStream?.close()
                } catch (_: Exception) {}

                val calculatedSha = recv.hasher.digest()
                val ok = calculatedSha.contentEquals(remoteSha) ||
                        recv.expectedSha256.all { it == 0.toByte() } ||
                        calculatedSha.contentEquals(recv.expectedSha256)

                mainHandler.post {
                    listener?.onTransferFinished(
                        id,
                        ok,
                        if (ok) "Verified" else "Checksum mismatch",
                    )
                }
            }

            TYPE_CANCEL -> {
                if (body.size < 4) return
                val id = readU32(body, 0)
                activeSends.remove(id)?.let { send ->
                    send.isCancelled.set(true)
                    try { send.inputStream.close() } catch (_: Exception) {}
                    mainHandler.post {
                        listener?.onTransferFinished(id, false, "Peer cancelled transfer")
                    }
                }
                activeRecvs.remove(id)?.let { recv ->
                    recv.isCancelled.set(true)
                    try { recv.outputStream?.close() } catch (_: Exception) {}
                    mainHandler.post {
                        listener?.onTransferFinished(id, false, "Peer cancelled transfer")
                    }
                }
            }
        }
    }

    private fun startPumpingSend(send: ActiveSend) {
        ioExecutor.execute {
            val buffer = ByteArray(CHUNK_SIZE)
            try {
                while (!send.isCancelled.get() && send.offset < send.size) {
                    val toRead = (send.size - send.offset).coerceAtMost(CHUNK_SIZE.toLong()).toInt()
                    val bytesRead = send.inputStream.read(buffer, 0, toRead)
                    if (bytesRead <= 0) break

                    val chunkMsg = ByteArrayOutputStream()
                    chunkMsg.write(VERSION.toInt())
                    chunkMsg.write(TYPE_CHUNK.toInt())
                    appendU32(chunkMsg, send.id)
                    appendU64(chunkMsg, send.offset)
                    appendU32(chunkMsg, bytesRead.toLong())
                    chunkMsg.write(buffer, 0, bytesRead)

                    sendBinary?.invoke(chunkMsg.toByteArray())

                    send.offset += bytesRead
                    val now = System.currentTimeMillis()
                    val elapsedMs = (now - send.startTimeMs).coerceAtLeast(1)
                    val speedBps = (send.offset.toDouble() / (elapsedMs.toDouble() / 1000.0))

                    mainHandler.post {
                        listener?.onTransferProgress(
                            send.id,
                            send.offset,
                            send.size,
                            speedBps,
                            elapsedMs / 1000,
                        )
                    }

                    // Pacing to avoid overloading WebRTC buffer
                    Thread.sleep(1)
                }

                if (!send.isCancelled.get() && send.offset >= send.size) {
                    val done = ByteArrayOutputStream()
                    done.write(VERSION.toInt())
                    done.write(TYPE_COMPLETE.toInt())
                    appendU32(done, send.id)
                    done.write(send.sha256)
                    sendBinary?.invoke(done.toByteArray())

                    activeSends.remove(send.id)
                    try { send.inputStream.close() } catch (_: Exception) {}

                    mainHandler.post {
                        listener?.onTransferFinished(send.id, true, "Complete")
                    }
                }
            } catch (e: Exception) {
                activeSends.remove(send.id)
                try { send.inputStream.close() } catch (_: Exception) {}
                mainHandler.post {
                    listener?.onTransferFinished(send.id, false, e.message ?: "Send error")
                }
            }
        }
    }

    fun stopAll() {
        for ((id, send) in activeSends) {
            send.isCancelled.set(true)
            try { send.inputStream.close() } catch (_: Exception) {}
        }
        activeSends.clear()

        for ((id, recv) in activeRecvs) {
            recv.isCancelled.set(true)
            try { recv.outputStream?.close() } catch (_: Exception) {}
        }
        activeRecvs.clear()
    }

    companion object {
        const val VERSION: Byte = 0x01
        const val TYPE_OFFER: Byte = 0x30
        const val TYPE_ACCEPT: Byte = 0x31
        const val TYPE_REJECT: Byte = 0x32
        const val TYPE_CHUNK: Byte = 0x33
        const val TYPE_COMPLETE: Byte = 0x34
        const val TYPE_CANCEL: Byte = 0x35

        const val CHUNK_SIZE = 32 * 1024

        fun appendU32(buf: ByteArrayOutputStream, value: Long) {
            buf.write((value and 0xFF).toInt())
            buf.write(((value shr 8) and 0xFF).toInt())
            buf.write(((value shr 16) and 0xFF).toInt())
            buf.write(((value shr 24) and 0xFF).toInt())
        }

        fun appendU64(buf: ByteArrayOutputStream, value: Long) {
            for (i in 0 until 8) {
                buf.write(((value shr (8 * i)) and 0xFF).toInt())
            }
        }

        fun readU32(bytes: ByteArray, offset: Int): Long {
            return (bytes[offset].toLong() and 0xFF) or
                    ((bytes[offset + 1].toLong() and 0xFF) shl 8) or
                    ((bytes[offset + 2].toLong() and 0xFF) shl 16) or
                    ((bytes[offset + 3].toLong() and 0xFF) shl 24)
        }

        fun readU64(bytes: ByteArray, offset: Int): Long {
            var v = 0L
            for (i in 0 until 8) {
                v = v or ((bytes[offset + i].toLong() and 0xFF) shl (8 * i))
            }
            return v
        }

        fun formatBytes(bytes: Long): String {
            if (bytes >= 1024 * 1024 * 1024) {
                return String.format("%.1f GB", bytes.toDouble() / (1024.0 * 1024.0 * 1024.0))
            }
            if (bytes >= 1024 * 1024) {
                return String.format("%.1f MB", bytes.toDouble() / (1024.0 * 1024.0))
            }
            if (bytes >= 1024) {
                return String.format("%.1f KB", bytes.toDouble() / 1024.0)
            }
            return "$bytes B"
        }
    }
}
