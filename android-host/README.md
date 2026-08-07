# Darpan Host (Android)

Native Android **host** client for Darpan / RemoteControl — equivalent to the **Windows Host** role.

Uses the production server: **`https://remotecontrol.sushrutmakes.qzz.io`** (same as desktop `SignalingClient` Origin header).

## Quick start

1. Build: `.\gradlew.bat assembleDebug`
2. Install: `adb install -r app\build\outputs\apk\debug\app-debug.apk`
3. Phone: open app → server URL (default production) → room ID → **Connect as Host**
4. Enable **Accessibility** → **Start screen sharing**
5. PC: Darpan desktop → same room as **viewer** + **controller**
6. PC: click **Allow Control** on host if controlling (Android has **Allow Control** toggle too)

## Desktop parity

| Desktop (Windows Host) | Android Host |
|------------------------|--------------|
| Socket.IO + Origin header | `SignalingClient` with `/socket.io/` + Origin |
| `join-room` appType host | Same |
| mediasoup transport setup | `MediasoupSession` |
| H.264 via `video-packet` | `ScreenCaptureService` MediaCodec → `video-packet` |
| `stream-ready` / `stream-stopped` | Same |
| `control` mouse/keyboard from controller | `ControlEventProcessor` + Accessibility |
| Allow Control toggle | Session screen switch |
| Kick peer | Long-press peer in list |
| `peer-ack` on join | Same |

## Sync from this repo

If you use a copy at `StudioProjects/android-host`, copy updated files from:

`C:\QTProjects\RemoteControl\android-host\`

Or re-copy the whole folder.

## Troubleshooting

| Issue | Fix |
|-------|-----|
| Compile errors `ACTION_IME_ACTION` / `defaultDisplay` | Pull latest `RemoteControlAccessibilityService.kt` |
| `join-room` never runs | Fixed: `onConnected` → `join-room` with 30s timeout + log panel |
| PC no video | Wait for keyframe; same room ID; sharing started on phone |
| PC control ignored | Accessibility on; PC role **controller**; **Allow Control** on both sides |
| Connection error | Use `https://remotecontrol.sushrutmakes.qzz.io` exactly |

See full setup in sections above (SDK, env vars, build commands).
