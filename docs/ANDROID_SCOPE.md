# Darpan Android — scope and limitations (v1)

## Supported roles

| Scenario | Android role | PC role | Remote control |
|----------|--------------|---------|----------------|
| Control Windows PC | **Controller** (join room) | **Host** | Yes — `request_control` → approve on PC; input on `darpan.control` per `docs/PROTOCOL.md` |
| Show phone screen to PC | **Host** (create room + MediaProjection) | **Viewer/controller** | **View-only from PC** — injecting input into Android is **not** implemented in v1 |

## Platform pairing

- Same **room codes** and signaling messages as desktop (`docs/ARCHITECTURE.md`, `docs/PROTOCOL.md`).
- Android client identifies as `client_platform: "android"` in `hello`.
- **No iOS client** — Darpan mobile is Android-only; there is no App Store target in this repo.

## Android host (screen share)

- Capture uses **MediaProjection** (user consent each session).
- Preview frames use **DPJ1** JPEG on `darpan.preview`, matching desktop host encoding.
- If a PC sends `request_control` while Android is host, the app shows a status message only; the host does **not** grant remote control of the device (no AccessibilityService in v1).

## Android controller (PC host)

- Receives preview on `darpan.preview`, sends mouse/keyboard on `darpan.control` after the PC host grants control.
- Touch is mapped to normalized coordinates using the stream width/height from the latest preview header.
- On-screen keyboard sends a small set of Windows virtual keys (Enter, Esc, Tab, etc.); full IME-to-VK mapping is not in v1.

## Network

- **Cleartext is disabled** (`usesCleartextTraffic="false"`, `network_security_config.xml`).
- Signaling URL is user-configurable in Settings; use **`wss://`** in production or place TLS in front of the Python server.
- STUN URL is configurable (default public Google STUN).

## Out of scope (v1)

- Android↔Android remote sessions
- iOS client
- Remote control of Android from PC (AccessibilityService / input injection)
- File transfer UI on Android (desktop `darpan.files` channel not wired in the mobile app yet)
