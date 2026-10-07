# Darpan desktop manual test checklist

## Prerequisites

1. Signaling server: `cd server && .\.venv\Scripts\python.exe server.py`
2. Built `Darpan.exe` with `datachannel.dll` beside it (`docs/BUILD.md`)
3. Two Windows PCs **or** one PC + VM on same LAN; for cross-LAN configure TURN in Settings + `compose/` (Phase 6)

## A — Room + video

| Step | Action | Expected |
|------|--------|----------|
| A1 | Host **Create room** | Room code shown |
| A2 | Joiner **Join room** | Both on room page; member list updates |
| A3 | Wait for peer connected | Host capture label shows **DXGI** or **GDI** fallback |
| A4 | Move windows on host | Joiner viewer updates |

## B — Control permission

| Step | Action | Expected |
|------|--------|----------|
| B1 | Joiner **Request control** | Host dialog: Allow / Deny |
| B2 | Host **Allow** | Joiner member row: **Control granted**; joiner can focus viewer |
| B3 | Joiner move mouse in viewer | Host cursor moves (SendInput) |
| B4 | Joiner type in viewer (click first) | Keys inject on host |
| B5 | Host **Revoke control** | Joiner returns to view-only; host ignores input |

## C — File transfer (P2P)

| Step | Action | Expected |
|------|--------|----------|
| C1 | Either side **Send file…** | Other side save dialog |
| C2 | Accept save path | Progress in file status label |
| C3 | Complete | SHA-256 verified message or mismatch reported |
| C4 | Wireshark on signaling server | No file payload on TCP 8765 |

## D — Reconnect

| Step | Action | Expected |
|------|--------|----------|
| D1 | Stop signaling briefly | Status warns; no auto P2P |
| D2 | **Reconnect signaling** then **Reconnect video** on host | Manual renegotiation |

## E — Cross-LAN (optional)

| Step | Action | Expected |
|------|--------|----------|
| E1 | Set STUN + TURN in Settings on both | ICE connects through TURN if symmetric NAT |
| E2 | Repeat A–C | Same behavior |

## Reference

- Binary formats: `docs/PROTOCOL.md`
- Capture fallback: DXGI Desktop Duplication → GDI BitBlt (see room **Capture** label)
