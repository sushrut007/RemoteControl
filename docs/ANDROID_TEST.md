# Darpan Android — testing

## Prerequisites

1. **Signaling server** on the dev machine:
   ```powershell
   cd C:\Gitlab-repo\darpan\server
   .\.venv\Scripts\python.exe server.py
   ```
2. **Desktop Darpan** built (`C:\Gitlab-repo\darpan\build\Release\Darpan.exe`) with signaling URL pointing at the same server as the phone/emulator.
3. **Android Studio** (Ladybug or newer) with SDK 35 and JDK 17.

Build:

```powershell
cd C:\Gitlab-repo\darpan\android
.\gradlew.bat assembleDebug
```

Install:

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

## Cleartext and signaling URL

The app **does not allow cleartext** HTTP/WebSocket. A default of `ws://10.0.2.2:8765/ws` in Settings will **fail** until you use TLS.

Options for local dev:

| Setup | Signaling URL on Android | Signaling URL on PC |
|--------|---------------------------|---------------------|
| **TLS reverse proxy** (recommended) | `wss://<host>:8443/ws` | same |
| **Emulator + adb reverse + WSS** | terminate TLS locally | same |

For quick LAN tests without TLS, run a small **Caddy/nginx** TLS terminator in front of `127.0.0.1:8765`, trust the cert on the device (or use a public dev hostname), and set **`wss://.../ws`** in Android Settings and desktop settings.

**Emulator note:** `10.0.2.2` is the host loopback from the Android emulator; use that hostname in the URL only when the signaling process listens on the host and TLS terminates on that host.

**Physical device:** use the PC’s LAN IP (or mDNS name) in the **wss** URL, same Wi‑Fi, firewall allows the TLS port.

## Test A — Android controls PC

1. Start signaling + `Darpan.exe`; **Host session** on PC → note room code (and PIN if set).
2. Android → **Settings** → device name, **wss** signaling URL, STUN → Save.
3. Android → **Control a PC** → enter code → **Join room**.
4. PC should negotiate WebRTC (PC creates offer when Android joins). Android shows the remote preview.
5. Android → **Request control** → approve on PC → touch moves mouse; **Keyboard** sends special keys.

## Test B — Android shares screen to PC

1. Android → **Share screen to PC** → accept MediaProjection → room created (code in toolbar).
2. PC → **Join** with same code.
3. PC should show Android screen (DPJ1 preview). Remote control of the phone from PC is **view-only** (see `docs/ANDROID_SCOPE.md`).

## Emulator specifics

- Prefer **x86_64** system image with Google APIs for faster WebRTC.
- If ICE stays disconnected, confirm STUN is reachable from the emulator and that both sides use the same room.
- Camera/mic permissions are not required (data-channel preview only).

## Real device specifics

- Disable battery optimization for Darpan if the OS kills the app during long sessions.
- MediaProjection shows the system consent dialog every time you start **Share screen to PC**.
- Keep the app **foreground** while hosting screen share.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| “WebSocket error” / instant disconnect | URL must be **wss** with valid cert; not `ws://` |
| Black preview | PC host sharing? ICE connected? Check desktop status line |
| Touch does nothing | **Request control** and host **Grant**; status should say control granted |
| Duplicate room members in UI | Reconnect signaling; **Leave room** on both sides |

## Automated checks

Server signaling tests (no Android):

```powershell
cd C:\Gitlab-repo\darpan\server
.\.venv\Scripts\python.exe -m pytest
```
