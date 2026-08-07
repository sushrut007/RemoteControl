package com.darpan.host

import android.app.Application

class DarpanHostApp : Application() {

    lateinit var preferences: HostPreferences
        private set

    lateinit var controlProcessor: ControlEventProcessor
        private set

    lateinit var signalingClient: SignalingClient
        private set

    lateinit var mediasoupSession: MediasoupSession
        private set

    lateinit var sessionManager: HostSessionManager
        private set

    override fun onCreate() {
        super.onCreate()
        instance = this
        preferences = HostPreferences(this)
        controlProcessor = ControlEventProcessor()

        var managerRef: HostSessionManager? = null
        signalingClient = SignalingClient { managerRef!! }
        mediasoupSession = MediasoupSession(signalingClient)
        sessionManager = HostSessionManager(
            signaling = signalingClient,
            mediasoup = mediasoupSession,
            controlProcessor = controlProcessor,
        )
        managerRef = sessionManager
    }

    companion object {
        lateinit var instance: DarpanHostApp
            private set

        val signalingClient: SignalingClient
            get() = instance.signalingClient
    }
}
