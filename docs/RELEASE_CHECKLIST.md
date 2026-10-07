# Darpan — release / demo checklist

Use this list before an internal demo or tagged release. All paths relative to repo root `C:\Gitlab-repo\darpan\`.

## 1. Secrets and repo hygiene

- [ ] No `.env`, PEM keys, or TURN passwords committed (`compose/docker-turn/.env` is gitignored).
- [ ] Room PINs only ever sent client→server at join; server stores **bcrypt hash** only.
- [ ] Production signaling uses **`DARPAN_TLS=1`** + valid certs (not plain `ws://` on the public internet).

## 2. Build artifacts

- [ ] Desktop: `cmake --build build --config Release` → `build\Release\Darpan.exe` runs.
- [ ] Server: `cd server && pytest` green.
- [ ] Android: `cd android && .\gradlew.bat assembleDebug` → APK installs.
- [ ] CI pipeline (`.gitlab-ci.yml`) reviewed for your runner tags.

## 3. Signaling and security

- [ ] Health check: `GET /health` returns OK.
- [ ] Clients use **`wss://…/ws`** (desktop Settings + Android Settings).
- [ ] Optional TLS pin tested if enabled (`DARPAN_SIGNALING_PIN_SHA256` / Android OkHttp pin).
- [ ] `pytest tests/test_forbidden_signaling_payloads.py` — media/file types rejected with `FORBIDDEN`.

## 4. P2P smoke tests (same LAN)

- [ ] PC host → PC join: preview visible, control grant/revoke works.
- [ ] PC host → Android join: preview + request control + touch.
- [ ] Android screen share → PC join: PC sees phone screen (view-only from PC).

See `docs/DESKTOP_TEST.md`, `docs/ANDROID_TEST.md`.

## 5. Cross-network (if demo requires it)

- [ ] `compose/docker-turn/` running on a host with public IP.
- [ ] **Same TURN URL/username/password** on both peers + STUN set.
- [ ] Test topology documented in `docs/CROSS_NETWORK.md` verified once (ICE connected, preview flows).

## 6. Privacy and lifecycle

- [ ] Desktop host shows **privacy banner** when peer connected / sharing.
- [ ] Android shows session banner in room UI.
- [ ] Closing desktop app or leaving room disconnects signaling and stops capture.
- [ ] Android **Leave room** or app exit calls leave on server.

## 7. Docs aligned

- [ ] `docs/USER_GUIDE.md` matches current UI strings.
- [ ] `docs/BUILD.md` paths and env vars match code.
- [ ] `docs/PROTOCOL.md` matches binary control/preview format in `ControlProtocol` (desktop + Android).

## 8. Demo script (5 minutes)

1. Start signaling (TLS) + show health URL.
2. Host on PC → share room code.
3. Join from second client → show view-only preview.
4. Request control → host approve → move mouse.
5. (Optional) Show Android controller or TURN cross-network slide from `docs/CROSS_NETWORK.md`.

---

**Version:** note `Darpan` / Android `versionName` in Gradle and `QApplication::setApplicationVersion` when tagging.
