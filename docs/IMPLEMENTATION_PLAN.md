# Darpan — Implementation Plan

Phases align with Cursor prompts 2–6. **Current phase: 6 (hardening / ship-readiness).**

## Phase 1 — Scaffold ✅ (this commit)

- Repo layout, CMake Qt 6.8.3 desktop shell, Python `server/` stub, Android Gradle stub
- Architecture and build docs
- No WebRTC, no live signaling

## Phase 2 — Python signaling server ✅

- FastAPI + WebSocket (or aiohttp) implementing protocol in `ARCHITECTURE.md`
- Room store in memory (Redis optional later)
- PIN hashing, rate limits, pytest for room/permission rules
- Dev: `python server.py` on port **8765**
- Two-client test script exchanging fake SDP

**Deliverable:** deployable signaling-only service + documented JSON schemas

## Phase 3 — Desktop: UI + signaling + P2P view-only ✅

## Phase 4 — Desktop: control + files ✅

## Phase 5 — Android ✅

- Kotlin app `com.darpan.remote`, Material 3 (Darpan dark tokens)
- OkHttp WebSocket signaling + Stream WebRTC (`darpan.preview` / `darpan.control`)
- Controller → PC (preview, touch, request control, basic keyboard)
- Host → MediaProjection screen share to PC (view-only control from PC)
- `docs/ANDROID_SCOPE.md`, `docs/ANDROID_TEST.md`

**Deliverable:** `assembleDebug` APK + test doc

## Phase 6 — Cross-network & release ✅

- `compose/docker-turn/` coturn + `.env.example`
- Client STUN/TURN in Settings + `DARPAN_*` env (desktop)
- TLS signaling (`DARPAN_TLS`), optional cert pin (desktop SHA-256 / Android OkHttp pin)
- Privacy indicators; clean shutdown on app exit
- `docs/BUILD.md`, `docs/USER_GUIDE.md`, `docs/CROSS_NETWORK.md`, `docs/RELEASE_CHECKLIST.md`
- `.gitlab-ci.yml` skeleton (server pytest, Android assembleDebug, optional Windows desktop)
- Forbidden payload pytest audit

**Deliverable:** demo-ready docs + CI skeleton

---

## CMake / Qt

- `cmake/CheckQtVersion.cmake` requires **Qt 6.8.3** exactly (patch flexible: 6.8.3+)
- Targets: `Darpan` executable
- Future: `FetchContent` for libdatachannel in Phase 3

## Module ownership

| Folder | Responsibility |
|--------|----------------|
| `Signaling/` | WS client, message serde |
| `RemoteSession/` | Peer connection, tracks, data channels |
| `Platform/Windows/` | DXGI/WGC capture, SendInput |
| `Ui/` | Main window, session views, QSS |
| `Utils/` | Logging, settings, crypto helpers |
| `server/server.py` | Signaling: rooms, relay, permissions |
| `android/app/` | Mobile UI + WebRTC |

---

## Testing strategy

| Layer | Tests |
|-------|--------|
| Server | pytest unit + WS integration script |
| Desktop | Manual checklist → later gtest for protocol serde |
| Android | Manual + emulator smoke |
| E2E | Two machines + optional TURN compose |

---

## Out of scope (v1)

- iOS client
- Android↔Android full remote control
- User accounts / OAuth on server
- Server-side recording or file storage
- Web browser client
