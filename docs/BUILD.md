# Darpan — Build and deploy

## Prerequisites

| Component | Version |
|-----------|---------|
| Windows | 10/11 x64 |
| CMake | 3.21+ |
| Qt | **6.8.3** (Widgets, WebSockets, Network) |
| MSVC | 2022 (14.x) |
| libdatachannel | Prebuilt (see below) |
| Python | 3.11+ (signaling server) |
| Android (optional) | JDK 17, SDK 35, Android Studio |

---

## Windows desktop (`Darpan.exe`)

### libdatachannel

Vendored in the repo (Qt is **not** vendored):

```text
third_party/libdatachannel/
  include/rtc/...
  bin/datachannel.lib
  bin/datachannel.dll
```

See `third_party/README.md`. Override path: `-DDARPAN_LIBDATACHANNEL_ROOT=...`

### Configure and build

```powershell
cd C:\Gitlab-repo\darpan
cmake -S . -B build `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release
.\build\Release\Darpan.exe
```

`datachannel.dll` is copied next to the executable at build time.

### Runtime settings (optional env overrides)

| Variable | Purpose |
|----------|---------|
| `DARPAN_SIGNALING_URL` | WebSocket URL (`wss://…/ws` in production) |
| `DARPAN_STUN_URL` | STUN URI |
| `DARPAN_TURN_URL` | TURN URI |
| `DARPAN_TURN_USERNAME` / `DARPAN_TURN_PASSWORD` | TURN credentials |
| `DARPAN_SIGNALING_PIN_SHA256` | TLS cert SHA-256 hex (enables pin when set) |

UI settings apply when env vars are unset.

### Capture

DXGI Desktop Duplication preferred; GDI fallback (`ScreenCapturerDxgi.cpp`). Live preview uses **DPJ1** on `darpan.preview` per `docs/PROTOCOL.md`.

Manual checklist: `docs/DESKTOP_TEST.md`.

---

## Python signaling server

### Local dev

```powershell
cd C:\Gitlab-repo\darpan\server
python -m venv .venv
.\.venv\Scripts\activate
pip install -e ".[dev]"
python server.py
```

Default (plain WS): `ws://127.0.0.1:8765/ws` — health: `http://127.0.0.1:8765/health`.

All signaling logic lives in **`server/server.py`**. Run: `python server.py`.

### TLS (production)

```powershell
$env:DARPAN_TLS = "1"
$env:DARPAN_SSL_CERT = "C:\path\signaling.crt"
$env:DARPAN_SSL_KEY = "C:\path\signaling.key"
$env:DARPAN_HOST = "0.0.0.0"
python server.py
```

Clients use **`wss://host:8765/ws`**. Room PINs are stored as **bcrypt hashes** only (in `server.py` / `RoomStore`).

### Docker (signaling only)

```powershell
cd C:\Gitlab-repo\darpan\server
docker build -t darpan-signaling .
docker run --rm -p 8765:8765 darpan-signaling
```

See `server/README.md` for all `DARPAN_*` variables.

### Tests

```powershell
cd C:\Gitlab-repo\darpan\server
pytest
```

---

## TURN (cross-network)

```bash
cd compose/docker-turn
cp .env.example .env
# Edit credentials + external IP; add user= line to turnserver.conf
docker compose up -d
```

Configure the same TURN URL and credentials on desktop and Android. Topology: `docs/CROSS_NETWORK.md`.

---

## Android debug APK

```powershell
cd C:\Gitlab-repo\darpan\android
.\gradlew.bat assembleDebug
```

Output: `app\build\outputs\apk\debug\app-debug.apk`

- Cleartext disabled — use **`wss://`** signaling in app Settings.
- Emulator/device notes: `docs/ANDROID_TEST.md`
- Scope: `docs/ANDROID_SCOPE.md`

---

## Troubleshooting

| Issue | Fix |
|-------|-----|
| Qt not found | Set `-DCMAKE_PREFIX_PATH` to Qt 6.8.3 MSVC kit |
| libdatachannel missing | Ensure `third_party/libdatachannel/` exists (see `third_party/README.md`) |
| ICE fails across networks | Deploy coturn; set TURN on **both** peers |
| Android WS fails | Use `wss://`, not `ws://` |
| TLS pin fails | Desktop: cert SHA-256 hex; Android: OkHttp `sha256/…` pin format |
